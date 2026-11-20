//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: ShaderCompiler3 -- compiles the native DX12 shader manifests to SM 6.0 DXIL with dxcompiler
//          (in process, every logical's combos on one thread pool, per-combo cache keyed on the preprocessed
//          source), writes the combo-index includes and VCS v6 files, generates the DX9 legacy combo-ABI
//          references, then validates and publishes everything (packer.cpp).
//
//          The include and combo semantics are ShaderCompile2's: directives are read from the main source and
//          every quoted include reachable from it (no include guards, case-insensitive file names), the
//          generated includes are byte-identical to ShaderCompile2's, and the compiled combo set is the one
//          its SKIP evaluation leaves.
//
//=============================================================================//
#include <windows.h>
#include <bcrypt.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include "LzmaLib.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace sc3 { void PackAndPublish( const fs::path &root, const fs::path &staging, const fs::path &game ); }

namespace
{

//-----------------------------------------------------------------------------
// Files and text
//-----------------------------------------------------------------------------
std::string ReadFile( const fs::path &path )
{
	std::ifstream in( path, std::ios::binary );
	if ( !in ) throw std::runtime_error( "Cannot read " + path.string() );
	std::ostringstream text; text << in.rdbuf();
	return text.str();
}

// Writes bytes to a temporary beside path. The name is unique per thread, so concurrent writers never share (and
// truncate) one temporary file.
fs::path WriteTemp( const fs::path &path, const std::string &bytes )
{
	fs::create_directories( path.parent_path() );
	const fs::path temp = path.string() + "." + std::to_string( GetCurrentThreadId() ) + ".tmp";
	std::ofstream out( temp, std::ios::binary | std::ios::trunc );
	if ( !out ) throw std::runtime_error( "Cannot write " + temp.string() );
	out.write( bytes.data(), std::streamsize( bytes.size() ) );
	out.close();
	if ( !out ) throw std::runtime_error( "Cannot write " + temp.string() );
	return temp;
}

void WriteFile( const fs::path &path, const std::string &bytes )
{
	fs::rename( WriteTemp( path, bytes ), path );
}

// Cache entries are content-addressed: if another writer (another process sharing -cache, or a reader holding the
// file open) wins the rename, the entry already holds the same bytes, so losing the race is not an error.
void WriteCacheFile( const fs::path &path, const std::string &bytes )
{
	const fs::path temp = WriteTemp( path, bytes );
	std::error_code error;
	fs::rename( temp, path, error );
	if ( error )
	{
		fs::remove( temp, error );
		if ( !fs::exists( path ) ) throw std::runtime_error( "Cannot store cache entry " + path.string() );
	}
}

std::string Lower( std::string text )
{
	for ( char &c : text ) c = char( std::tolower( static_cast<unsigned char>( c ) ) );
	return text;
}

std::string Trim( const std::string &text )
{
	size_t begin = 0, end = text.size();
	while ( begin < end && std::isspace( static_cast<unsigned char>( text[begin] ) ) ) ++begin;
	while ( end > begin && std::isspace( static_cast<unsigned char>( text[end - 1] ) ) ) --end;
	return text.substr( begin, end - begin );
}

std::vector<std::string> Lines( const std::string &text )
{
	std::vector<std::string> lines;
	size_t start = 0;
	for ( size_t i = 0; i <= text.size(); ++i )
	{
		if ( i == text.size() || text[i] == '\n' )
		{
			std::string line = text.substr( start, i - start );
			if ( !line.empty() && line.back() == '\r' ) line.pop_back();
			if ( i < text.size() || !line.empty() ) lines.push_back( std::move( line ) );
			start = i + 1;
		}
	}
	return lines;
}

std::wstring Widen( const std::string &text ) { return std::wstring( text.begin(), text.end() ); }

std::string Hex( const uint8_t *bytes, size_t count )
{
	static const char digits[] = "0123456789abcdef";
	std::string out;
	for ( size_t i = 0; i < count; ++i ) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
	return out;
}

std::string Sha256( const std::string &data )
{
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	uint8_t digest[32] = {};
	bool ok = BCRYPT_SUCCESS( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) ) &&
	          BCRYPT_SUCCESS( BCryptCreateHash( algorithm, &hash, nullptr, 0, nullptr, 0, 0 ) ) &&
	          BCRYPT_SUCCESS( BCryptHashData( hash, reinterpret_cast<PUCHAR>( const_cast<char *>( data.data() ) ), ULONG( data.size() ), 0 ) ) &&
	          BCRYPT_SUCCESS( BCryptFinishHash( hash, digest, sizeof( digest ), 0 ) );
	if ( hash ) BCryptDestroyHash( hash );
	if ( algorithm ) BCryptCloseAlgorithmProvider( algorithm, 0 );
	if ( !ok ) throw std::runtime_error( "SHA-256 failed" );
	return Hex( digest, sizeof( digest ) );
}

//-----------------------------------------------------------------------------
// Source trees: file name (case-insensitive) -> text
//-----------------------------------------------------------------------------
struct SourceTree
{
	std::unordered_map<std::string, std::pair<fs::path, std::string>> files;

	void Add( const fs::path &path )
	{
		const std::string key = Lower( path.filename().string() );
		if ( files.count( key ) ) throw std::runtime_error( "Duplicate HLSL file name in native trees: " + path.filename().string() );
		files[key] = { path, ReadFile( path ) };
	}
	const std::pair<fs::path, std::string> *Find( const std::string &name ) const
	{
		auto it = files.find( Lower( fs::path( name ).filename().string() ) );
		return it == files.end() ? nullptr : &it->second;
	}
};

//-----------------------------------------------------------------------------
// Combo directives
//-----------------------------------------------------------------------------
enum class DirectiveKind { Static, Dynamic, Skip, Centroid };
struct Directive { DirectiveKind kind; std::string text; };

// "// STATIC: ..." with optional spaces; the text is trimmed.
bool ParseDirectiveLine( const std::string &line, Directive &out )
{
	size_t i = 0;
	while ( i < line.size() && std::isspace( static_cast<unsigned char>( line[i] ) ) ) ++i;
	if ( line.compare( i, 2, "//" ) ) return false;
	i += 2;
	while ( i < line.size() && std::isspace( static_cast<unsigned char>( line[i] ) ) ) ++i;
	static const std::pair<const char *, DirectiveKind> kinds[] = {
		{ "STATIC", DirectiveKind::Static }, { "DYNAMIC", DirectiveKind::Dynamic },
		{ "SKIP", DirectiveKind::Skip }, { "CENTROID", DirectiveKind::Centroid } };
	for ( const auto &kind : kinds )
	{
		const size_t length = strlen( kind.first );
		if ( line.compare( i, length, kind.first ) ) continue;
		size_t j = i + length;
		while ( j < line.size() && std::isspace( static_cast<unsigned char>( line[j] ) ) ) ++j;
		if ( j >= line.size() || line[j] != ':' ) return false;
		out.kind = kind.second;
		out.text = Trim( line.substr( j + 1 ) );
		return true;
	}
	return false;
}

bool ParseIncludeLine( const std::string &line, std::string &name )
{
	size_t i = 0;
	while ( i < line.size() && std::isspace( static_cast<unsigned char>( line[i] ) ) ) ++i;
	if ( i >= line.size() || line[i] != '#' ) return false;
	++i;
	while ( i < line.size() && std::isspace( static_cast<unsigned char>( line[i] ) ) ) ++i;
	if ( line.compare( i, 7, "include" ) ) return false;
	i += 7;
	while ( i < line.size() && std::isspace( static_cast<unsigned char>( line[i] ) ) ) ++i;
	if ( i >= line.size() || line[i] != '"' ) return false;
	const size_t end = line.find( '"', i + 1 );
	if ( end == std::string::npos ) return false;
	name = line.substr( i + 1, end - i - 1 );
	return true;
}

// Depth-first over quoted includes found in the tree, without include guards (ShaderCompile2 semantics).
void CollectDirectives( const SourceTree &tree, const std::string &text, std::vector<Directive> &out, int depth = 0 )
{
	if ( depth > 64 ) throw std::runtime_error( "Include recursion too deep" );
	for ( const std::string &line : Lines( text ) )
	{
		Directive directive;
		std::string include;
		if ( ParseDirectiveLine( line, directive ) ) out.push_back( directive );
		else if ( ParseIncludeLine( line, include ) )
			if ( const auto *file = tree.Find( include ) )
				CollectDirectives( tree, file->second, out, depth + 1 );
	}
}

// Legacy DX9 directives carry platform ([PC]/[XBOX]) and model ([ps20b], [vs30], ...) tags: keep untagged or matching
// ones and drop the tags ("[= init]" stays).
std::vector<Directive> FilterLegacy( const std::vector<Directive> &in, const std::string &target )
{
	std::vector<Directive> out;
	for ( const Directive &directive : in )
	{
		std::vector<std::string> platforms, models;
		std::string stripped;
		const std::string &text = directive.text;
		for ( size_t i = 0; i < text.size(); )
		{
			if ( text[i] == '[' && i + 1 < text.size() && text[i + 1] != '=' && text.find( ']', i ) != std::string::npos )
			{
				const size_t end = text.find( ']', i );
				const std::string tag = Trim( text.substr( i + 1, end - i - 1 ) );
				( tag == "PC" || tag == "XBOX" ? platforms : models ).push_back( tag );
				while ( !stripped.empty() && std::isspace( static_cast<unsigned char>( stripped.back() ) ) ) stripped.pop_back();
				i = end + 1;
				continue;
			}
			stripped += text[i++];
		}
		if ( !platforms.empty() && std::find( platforms.begin(), platforms.end(), "PC" ) == platforms.end() ) continue;
		if ( !models.empty() && std::find( models.begin(), models.end(), target ) == models.end() ) continue;
		out.push_back( { directive.kind, stripped } );
	}
	return out;
}

struct Combo { std::string name; int low = 0, high = 0; std::string init; bool hasInit = false; };

Combo ParseCombo( std::string text )
{
	const size_t comment = text.find( "//" );
	if ( comment != std::string::npos ) text = Trim( text.substr( 0, comment ) );
	Combo combo;
	size_t q0 = text.find( '"' ), q1 = q0 == std::string::npos ? q0 : text.find( '"', q0 + 1 );
	size_t q2 = q1 == std::string::npos ? q1 : text.find( '"', q1 + 1 ), q3 = q2 == std::string::npos ? q2 : text.find( '"', q2 + 1 );
	if ( q3 == std::string::npos ) throw std::runtime_error( "Unsupported combo directive: " + text );
	combo.name = text.substr( q0 + 1, q1 - q0 - 1 );
	const std::string range = text.substr( q2 + 1, q3 - q2 - 1 );
	const size_t dots = range.find( ".." );
	if ( dots == std::string::npos ) throw std::runtime_error( "Unsupported combo range: " + text );
	combo.low = std::stoi( range.substr( 0, dots ) );
	combo.high = std::stoi( range.substr( dots + 2 ) );
	const std::string rest = Trim( text.substr( q3 + 1 ) );
	const size_t open = rest.find( "[=" );
	if ( open != std::string::npos && !rest.empty() && rest.back() == ']' )
	{
		combo.hasInit = true;
		combo.init = Trim( rest.substr( open + 2, rest.size() - open - 3 ) );
	}
	if ( combo.high < combo.low ) throw std::runtime_error( "Empty combo range: " + text );
	return combo;
}

//-----------------------------------------------------------------------------
// SKIP expressions: || and && right-associative, ! unary, comparisons, $VAR, defined $VAR, numbers, bare names
//-----------------------------------------------------------------------------
struct Expr
{
	enum Kind { Binary, Not, Var, Defined, Number, Ident } kind;
	std::string text;	// operator, variable/identifier name or number
	std::unique_ptr<Expr> left, right;
};

class ExprParser
{
public:
	explicit ExprParser( const std::string &source ) { Tokenize( source ); }
	std::unique_ptr<Expr> Parse()
	{
		auto e = Or();
		if ( m_Next != m_Tokens.size() ) throw std::runtime_error( "Trailing tokens in SKIP expression" );
		return e;
	}
private:
	void Tokenize( const std::string &s )
	{
		for ( size_t i = 0; i < s.size(); )
		{
			const char c = s[i];
			if ( std::isspace( static_cast<unsigned char>( c ) ) ) { ++i; continue; }
			static const char *const twoChar[] = { "||", "&&", "==", "!=", "<=", ">=" };
			bool matched = false;
			for ( const char *op : twoChar )
				if ( !s.compare( i, 2, op ) ) { m_Tokens.push_back( op ); i += 2; matched = true; break; }
			if ( matched ) continue;
			if ( strchr( "()!<>", c ) ) { m_Tokens.push_back( std::string( 1, c ) ); ++i; continue; }
			if ( c == '$' || std::isalnum( static_cast<unsigned char>( c ) ) || c == '_' )
			{
				size_t j = i + 1;
				while ( j < s.size() && ( std::isalnum( static_cast<unsigned char>( s[j] ) ) || s[j] == '_' ) ) ++j;
				m_Tokens.push_back( s.substr( i, j - i ) );
				i = j;
				continue;
			}
			throw std::runtime_error( "Unsupported SKIP expression: " + s );
		}
	}
	const std::string *Peek() const { return m_Next < m_Tokens.size() ? &m_Tokens[m_Next] : nullptr; }
	std::string Take()
	{
		if ( m_Next >= m_Tokens.size() ) throw std::runtime_error( "Truncated SKIP expression" );
		return m_Tokens[m_Next++];
	}
	std::unique_ptr<Expr> MakeBinary( const std::string &op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r )
	{
		auto e = std::make_unique<Expr>(); e->kind = Expr::Binary; e->text = op; e->left = std::move( l ); e->right = std::move( r );
		return e;
	}
	std::unique_ptr<Expr> Or()
	{
		auto l = And();
		if ( Peek() && *Peek() == "||" ) { Take(); return MakeBinary( "||", std::move( l ), Or() ); }
		return l;
	}
	std::unique_ptr<Expr> And()
	{
		auto l = Unary();
		if ( Peek() && *Peek() == "&&" ) { Take(); return MakeBinary( "&&", std::move( l ), And() ); }
		return l;
	}
	std::unique_ptr<Expr> Unary()
	{
		if ( Peek() && *Peek() == "!" )
		{
			Take();
			auto e = std::make_unique<Expr>(); e->kind = Expr::Not; e->left = Unary();
			return e;
		}
		return Compare();
	}
	std::unique_ptr<Expr> Compare()
	{
		auto l = Primary();
		if ( const std::string *op = Peek() )
			if ( *op == "==" || *op == "!=" || *op == "<" || *op == ">" || *op == "<=" || *op == ">=" )
			{
				const std::string o = Take();
				return MakeBinary( o, std::move( l ), Primary() );
			}
		return l;
	}
	std::unique_ptr<Expr> Primary()
	{
		const std::string t = Take();
		auto e = std::make_unique<Expr>();
		if ( t == "(" )
		{
			auto inner = Or();
			if ( Take() != ")" ) throw std::runtime_error( "Unbalanced SKIP expression" );
			return inner;
		}
		if ( t == "defined" )
		{
			const std::string v = Take();
			if ( v.empty() || v[0] != '$' ) throw std::runtime_error( "defined without $VAR in SKIP expression" );
			e->kind = Expr::Defined; e->text = v.substr( 1 );
		}
		else if ( t[0] == '$' ) { e->kind = Expr::Var; e->text = t.substr( 1 ); }
		else if ( std::isdigit( static_cast<unsigned char>( t[0] ) ) ) { e->kind = Expr::Number; e->text = t; }
		else if ( std::isalpha( static_cast<unsigned char>( t[0] ) ) || t[0] == '_' ) { e->kind = Expr::Ident; e->text = t; }
		else throw std::runtime_error( "Unexpected token in SKIP expression: " + t );
		return e;
	}
	std::vector<std::string> m_Tokens;
	size_t m_Next = 0;
};

// Referenced combo names; a bare identifier is never a combo ("#" marks it).
void ExprVars( const Expr &e, std::set<std::string> &vars )
{
	switch ( e.kind )
	{
	case Expr::Binary: ExprVars( *e.left, vars ); ExprVars( *e.right, vars ); break;
	case Expr::Not: ExprVars( *e.left, vars ); break;
	case Expr::Var: case Expr::Defined: vars.insert( e.text ); break;
	case Expr::Ident: vars.insert( "#" + e.text ); break;
	default: break;
	}
}

int ExprEval( const Expr &e, const std::map<std::string, int> &values )
{
	switch ( e.kind )
	{
	case Expr::Binary:
	{
		const int a = ExprEval( *e.left, values ), b = ExprEval( *e.right, values );
		const std::string &o = e.text;
		if ( o == "&&" ) return a && b;
		if ( o == "||" ) return a || b;
		if ( o == "==" ) return a == b;
		if ( o == "!=" ) return a != b;
		if ( o == "<" ) return a < b;
		if ( o == ">" ) return a > b;
		if ( o == "<=" ) return a <= b;
		return a >= b;
	}
	case Expr::Not: return !ExprEval( *e.left, values );
	case Expr::Var: { auto it = values.find( e.text ); return it == values.end() ? 0 : it->second; }
	case Expr::Defined: return values.count( e.text ) ? 1 : 0;
	case Expr::Number: return std::stoi( e.text );
	default: return 0;
	}
}

std::string ExprPrint( const Expr &e, const char *prefix )
{
	switch ( e.kind )
	{
	case Expr::Binary: return "( " + ExprPrint( *e.left, prefix ) + " " + e.text + " " + ExprPrint( *e.right, prefix ) + " )";
	case Expr::Not: return "!" + ExprPrint( *e.left, prefix );
	case Expr::Var: return prefix + e.text;
	case Expr::Defined: return "1";
	default: return e.text;
	}
}

//-----------------------------------------------------------------------------
// One shader's combo layout and its ShaderCompile2-format include
//-----------------------------------------------------------------------------
struct ComboLayout
{
	std::vector<Combo> statics, dynamics;
	std::vector<std::string> skipText;
	std::vector<std::unique_ptr<Expr>> skips;
	uint32_t centroidMask = 0;

	static uint64_t Count( const std::vector<Combo> &combos )
	{
		uint64_t count = 1;
		for ( const Combo &c : combos ) count *= uint64_t( c.high - c.low + 1 );
		return count;
	}
	uint64_t StaticCount() const { return Count( statics ); }
	uint64_t DynamicCount() const { return Count( dynamics ); }

	void Values( uint64_t staticIndex, uint64_t dynamicIndex, std::map<std::string, int> &values ) const
	{
		values.clear();
		for ( const Combo &c : statics ) { const uint64_t n = c.high - c.low + 1; values[c.name] = c.low + int( staticIndex % n ); staticIndex /= n; }
		for ( const Combo &c : dynamics ) { const uint64_t n = c.high - c.low + 1; values[c.name] = c.low + int( dynamicIndex % n ); dynamicIndex /= n; }
	}
	bool Skipped( const std::map<std::string, int> &values ) const
	{
		for ( const auto &skip : skips ) if ( ExprEval( *skip, values ) ) return true;
		return false;
	}
};

ComboLayout BuildLayout( const std::vector<Directive> &directives )
{
	ComboLayout layout;
	for ( const Directive &d : directives )
	{
		switch ( d.kind )
		{
		case DirectiveKind::Static: layout.statics.push_back( ParseCombo( d.text ) ); break;
		case DirectiveKind::Dynamic: layout.dynamics.push_back( ParseCombo( d.text ) ); break;
		case DirectiveKind::Skip:
			layout.skipText.push_back( d.text );
			layout.skips.push_back( ExprParser( d.text ).Parse() );
			break;
		case DirectiveKind::Centroid:
		{
			const std::string t = Trim( d.text );
			if ( t.compare( 0, 8, "TEXCOORD" ) || t.size() == 8 ) throw std::runtime_error( "Unsupported CENTROID directive: " + t );
			layout.centroidMask |= 1u << std::stoi( t.substr( 8 ) );
			break;
		}
		}
	}
	return layout;
}

int BitLength( uint64_t value ) { int bits = 0; while ( value ) { ++bits; value >>= 1; } return bits; }

std::string ComboInclude( const std::string &base, const std::string &stage, const ComboLayout &layout )
{
	std::vector<std::string> lines;
	if ( !layout.skipText.empty() )
	{
		lines.push_back( "// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!" );
		for ( const std::string &skip : layout.skipText ) lines.push_back( "// " + skip );
		lines.push_back( "" );
	}
	lines.push_back( "#pragma once" );
	lines.push_back( "#include \"shaderlib/cshader.h\"" );
	const std::string prefix = stage == "vs" ? "vsh" : stage == "ps" ? "psh" : "csh";
	const struct { const char *kind; const std::vector<Combo> *combos; uint64_t stride; } classes[] = {
		{ "Static", &layout.statics, layout.DynamicCount() }, { "Dynamic", &layout.dynamics, 1 } };
	for ( const auto &cls : classes )
	{
		const std::string name = base + "_" + cls.kind + "_Index";
		std::vector<const Combo *> manual;
		std::set<std::string> names;
		for ( const Combo &c : *cls.combos ) { names.insert( c.name ); if ( !c.hasInit ) manual.push_back( &c ); }
		lines.push_back( "class " + name );
		lines.push_back( "{" );
		for ( const Combo &c : *cls.combos ) lines.push_back( "\tunsigned int m_n" + c.name + " : " + std::to_string( BitLength( uint64_t( c.high - c.low + 1 ) ) ) + ";" );
		if ( !manual.empty() )
		{
			lines.push_back( "#ifdef _DEBUG" );
			for ( const Combo *c : manual ) lines.push_back( "\tbool m_b" + c->name + " : 1;" );
			lines.push_back( "#endif\t// _DEBUG" );
		}
		lines.push_back( "public:" );
		for ( const Combo &c : *cls.combos )
		{
			lines.push_back( "\tvoid Set" + c.name + "( int i )" );
			lines.push_back( "\t{" );
			lines.push_back( "\t\tAssert( i >= " + std::to_string( c.low ) + " && i <= " + std::to_string( c.high ) + " );" );
			lines.push_back( c.low == 0 ? "\t\tm_n" + c.name + " = i;" : "\t\tm_n" + c.name + " = i - " + std::to_string( c.low ) + ";" );
			if ( !c.hasInit )
			{
				lines.push_back( "#ifdef _DEBUG" );
				lines.push_back( "\t\tm_b" + c.name + " = true;" );
				lines.push_back( "#endif\t// _DEBUG" );
			}
			lines.push_back( "\t}" );
			lines.push_back( "" );
		}
		lines.push_back( "\t" + name + "(  )" );
		lines.push_back( "\t{" );
		for ( const Combo &c : *cls.combos ) lines.push_back( "\t\tm_n" + c.name + " = " + ( c.hasInit ? c.init : "0" ) + ";" );
		if ( !manual.empty() )
		{
			lines.push_back( "#ifdef _DEBUG" );
			for ( const Combo *c : manual ) lines.push_back( "\t\tm_b" + c->name + " = false;" );
			lines.push_back( "#endif\t// _DEBUG" );
		}
		lines.push_back( "\t}" );
		lines.push_back( "" );
		lines.push_back( "\tint GetIndex() const" );
		lines.push_back( "\t{" );
		if ( !manual.empty() )
		{
			std::string assert = "\t\tAssert( ";
			for ( size_t i = 0; i < manual.size(); ++i ) assert += ( i ? " && m_b" : "m_b" ) + manual[i]->name;
			lines.push_back( assert + " );" );
		}
		for ( size_t s = 0; s < layout.skips.size(); ++s )
		{
			std::set<std::string> vars;
			ExprVars( *layout.skips[s], vars );
			if ( vars.empty() || !std::includes( names.begin(), names.end(), vars.begin(), vars.end() ) ) continue;
			const Expr &e = *layout.skips[s];
			const std::string full = ExprPrint( e, "" ), inner = ExprPrint( e, "m_n" );
			const std::string negated = e.kind == Expr::Binary ? "!( " + inner.substr( 2, inner.size() - 4 ) + " )" : "!" + inner;
			lines.push_back( "\t\tAssertMsg( " + negated + ", \"Invalid combo combination " + full + "\" );" );
		}
		std::string sum = "\t\treturn ";
		uint64_t stride = cls.stride;
		for ( const Combo &c : *cls.combos )
		{
			sum += "( " + std::to_string( stride ) + " * m_n" + c.name + " ) + ";
			stride *= uint64_t( c.high - c.low + 1 );
		}
		lines.push_back( sum + "0;" );
		lines.push_back( "\t}" );
		lines.push_back( "};" );
		lines.push_back( "" );
		std::string tests;
		for ( const Combo *c : manual ) tests += ( tests.empty() ? "" : " + " ) + prefix + "_forgot_to_set_" + Lower( cls.kind ) + "_" + c->name;
		lines.push_back( std::string( "#define shader" ) + cls.kind + "Test_" + base + " " + ( tests.empty() ? "1" : tests ) );
		lines.push_back( "" );
		lines.push_back( "" );
	}
	std::string out;
	for ( size_t i = 0; i < lines.size(); ++i ) { if ( i ) out += '\n'; out += lines[i]; }
	return out;
}

//-----------------------------------------------------------------------------
// VCS v6: header, static records (+ terminator), duplicate statics, then LZMA blocks of
// [dynamic id][length][program] records closed by 0xffffffff.
//-----------------------------------------------------------------------------
void PutU32( std::string &out, uint32_t value ) { out.append( reinterpret_cast<const char *>( &value ), 4 ); }

// Source's LZMA envelope ("LZMA", actual size, packed size, 5 property bytes, raw LZMA stream; CLZMA decodes it),
// tagged as an LZMA block (0x40000000 | envelope size).
std::string LzmaBlock( const std::string &raw )
{
	constexpr unsigned kDictionary = 1u << 20;
	size_t packedSize = raw.size() + raw.size() / 3 + 256;
	std::string packed( packedSize, '\0' );
	unsigned char props[5] = {};
	size_t propsSize = sizeof( props );
	if ( LzmaCompress( reinterpret_cast<unsigned char *>( &packed[0] ), &packedSize, reinterpret_cast<const unsigned char *>( raw.data() ), raw.size(),
	         props, &propsSize, 9, kDictionary, 3, 0, 2, 64, 1 ) != SZ_OK || propsSize != sizeof( props ) )
		throw std::runtime_error( "LZMA compression failed" );
	packed.resize( packedSize );
	std::string envelope = "LZMA";
	PutU32( envelope, uint32_t( raw.size() ) );
	PutU32( envelope, uint32_t( packed.size() ) );
	envelope.append( reinterpret_cast<const char *>( props ), sizeof( props ) );
	envelope += packed;
	if ( envelope.size() >= 0x40000000u ) throw std::runtime_error( "VCS block too large" );
	std::string out;
	PutU32( out, uint32_t( envelope.size() ) | 0x40000000u );
	return out + envelope;
}

std::string WriteVcs( const ComboLayout &layout, const std::map<uint32_t, std::vector<std::pair<uint32_t, const std::string *>>> &programs,
	uint32_t sourceHash )
{
	constexpr size_t kBlockBytes = 1u << 20;
	std::vector<std::pair<uint32_t, std::string>> blocks;
	std::vector<std::pair<uint32_t, uint32_t>> duplicates;
	std::map<std::vector<std::pair<uint32_t, const std::string *>>, uint32_t> canonical;
	for ( const auto &[staticId, list] : programs )
	{
		std::vector<std::pair<uint32_t, const std::string *>> key = list;
		auto found = canonical.end();
		for ( auto it = canonical.begin(); it != canonical.end(); ++it )
		{
			if ( it->first.size() != key.size() ) continue;
			bool same = true;
			for ( size_t i = 0; same && i < key.size(); ++i )
				same = it->first[i].first == key[i].first && *it->first[i].second == *key[i].second;
			if ( same ) { found = it; break; }
		}
		if ( found != canonical.end() ) { duplicates.push_back( { staticId, found->second } ); continue; }
		canonical.emplace( key, staticId );
		std::string block, pending;
		for ( const auto &[dynamicId, bytes] : list )
		{
			std::string record;
			PutU32( record, dynamicId );
			PutU32( record, uint32_t( bytes->size() ) );
			record += *bytes;
			if ( !pending.empty() && pending.size() + record.size() > kBlockBytes ) { block += LzmaBlock( pending ); pending.clear(); }
			pending += record;
		}
		if ( !pending.empty() ) block += LzmaBlock( pending );
		PutU32( block, 0xffffffffu );
		blocks.push_back( { staticId, std::move( block ) } );
	}
	const uint32_t records = uint32_t( blocks.size() + 1 );
	uint32_t offset = 28 + 8 * records + 4 + 8 * uint32_t( duplicates.size() );
	std::string out;
	PutU32( out, 6 );
	PutU32( out, uint32_t( layout.StaticCount() * layout.DynamicCount() ) );
	PutU32( out, uint32_t( layout.DynamicCount() ) );
	PutU32( out, 0 );
	PutU32( out, layout.centroidMask );
	PutU32( out, records );
	PutU32( out, sourceHash );
	for ( const auto &[staticId, block] : blocks ) { PutU32( out, staticId ); PutU32( out, offset ); offset += uint32_t( block.size() ); }
	PutU32( out, 0xffffffffu );
	PutU32( out, offset );
	PutU32( out, uint32_t( duplicates.size() ) );
	for ( const auto &[alias, target] : duplicates ) { PutU32( out, alias ); PutU32( out, target ); }
	for ( const auto &[staticId, block] : blocks ) out += block;
	return out;
}

//-----------------------------------------------------------------------------
// dxcompiler: per-thread compiler, include handler over the flattened native tree
//-----------------------------------------------------------------------------
class TreeIncludeHandler : public IDxcIncludeHandler
{
public:
	TreeIncludeHandler( const SourceTree &tree, IDxcUtils *utils ) : m_Tree( tree ), m_Utils( utils ) {}
	HRESULT STDMETHODCALLTYPE LoadSource( LPCWSTR pFilename, IDxcBlob **ppIncludeSource ) override
	{
		std::wstring wide( pFilename );
		std::string name( wide.begin(), wide.end() );
		const auto *file = m_Tree.Find( name );
		if ( !file ) { *ppIncludeSource = nullptr; return E_FAIL; }
		ComPtr<IDxcBlobEncoding> blob;
		HRESULT hr = m_Utils->CreateBlobFromPinned( file->second.data(), UINT32( file->second.size() ), DXC_CP_UTF8, &blob );
		*ppIncludeSource = blob.Detach();
		return hr;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **ppv ) override
	{
		if ( riid == __uuidof( IDxcIncludeHandler ) || riid == __uuidof( IUnknown ) ) { *ppv = this; return S_OK; }
		*ppv = nullptr; return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
	ULONG STDMETHODCALLTYPE Release() override { return 1; }
private:
	const SourceTree &m_Tree;
	IDxcUtils *m_Utils;
};

struct DxcApi
{
	HMODULE module = nullptr;
	DxcCreateInstanceProc create = nullptr;
	std::string version;
};

DxcApi LoadDxc()
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW( nullptr, path, MAX_PATH );
	fs::path dll = fs::path( path ).parent_path() / "dxcompiler.dll";
	DxcApi api;
	api.module = LoadLibraryExW( dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
	if ( !api.module ) throw std::runtime_error( "dxcompiler.dll missing beside ShaderCompiler3.exe" );
	if ( !LoadLibraryExW( ( fs::path( path ).parent_path() / "dxil.dll" ).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH ) )
		throw std::runtime_error( "dxil.dll (DXIL validator/signer) missing beside ShaderCompiler3.exe" );
	api.create = reinterpret_cast<DxcCreateInstanceProc>( GetProcAddress( api.module, "DxcCreateInstance" ) );
	if ( !api.create ) throw std::runtime_error( "dxcompiler.dll has no DxcCreateInstance" );
	ComPtr<IDxcCompiler3> compiler;
	ComPtr<IDxcVersionInfo> info;
	if ( FAILED( api.create( CLSID_DxcCompiler, IID_PPV_ARGS( &compiler ) ) ) || FAILED( compiler.As( &info ) ) )
		throw std::runtime_error( "dxcompiler.dll failed to create its compiler" );
	UINT32 major = 0, minor = 0;
	info->GetVersion( &major, &minor );
	api.version = std::to_string( major ) + "." + std::to_string( minor );
	ComPtr<IDxcVersionInfo2> info2;
	if ( SUCCEEDED( compiler.As( &info2 ) ) )
	{
		UINT32 commits = 0; char *hash = nullptr;
		if ( SUCCEEDED( info2->GetCommitInfo( &commits, &hash ) ) && hash )
		{
			api.version += std::string( "+" ) + hash + "." + std::to_string( commits );
			CoTaskMemFree( hash );
		}
	}
	return api;
}

std::string ResultErrors( IDxcResult *result )
{
	ComPtr<IDxcBlobUtf8> errors;
	if ( SUCCEEDED( result->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( &errors ), nullptr ) ) && errors && errors->GetStringLength() )
		return std::string( errors->GetStringPointer(), errors->GetStringLength() );
	return {};
}

//-----------------------------------------------------------------------------
// Manifests
//-----------------------------------------------------------------------------
struct ManifestItem
{
	std::string source, stage, logical, profile, legacy;	// legacy: DX9 source (empty for native-only)
	std::string compiledBase;
};

std::string CompiledBase( const std::string &source, const std::string &stage )
{
	std::string stem = fs::path( source ).stem().string();
	static const char *const suffixes[] = { "2x", "20b", "20", "30", "40", "41", "50", "51", "xx" };
	for ( const char *s : { "_vs", "_ps", "_cs" } )
		for ( const char *v : suffixes )
		{
			const std::string tail = std::string( s ) + v;
			if ( stem.size() > tail.size() && !stem.compare( stem.size() - tail.size(), tail.size(), tail ) )
				return stem.substr( 0, stem.size() - tail.size() ) + "_" + stage + "51";
		}
	return stem + "_" + stage + "51";
}

std::string LegacyBase( const std::string &legacy, const std::string &stage, const std::string &profile )
{
	const std::string token = profile == "30" ? "30" : stage == "vs" ? "20" : "20b";
	std::string stem = fs::path( legacy ).stem().string();
	static const char *const suffixes[] = { "2x", "20b", "20", "30", "11", "xx" };
	for ( const char *s : { "_vs", "_ps" } )
		for ( const char *v : suffixes )
		{
			const std::string tail = std::string( s ) + v;
			if ( stem.size() > tail.size() && !stem.compare( stem.size() - tail.size(), tail.size(), tail ) )
			{
				stem = stem.substr( 0, stem.size() - tail.size() );
				return stem + "_" + stage + token;
			}
		}
	return stem + "_" + stage + token;
}

std::vector<ManifestItem> ReadManifests( const fs::path &root )
{
	std::vector<fs::path> files;
	for ( const auto &entry : fs::directory_iterator( root / "manifests" ) )
		if ( entry.path().extension() == ".txt" ) files.push_back( entry.path() );
	std::sort( files.begin(), files.end() );
	std::vector<ManifestItem> items;
	for ( const fs::path &file : files )
	{
		for ( const std::string &raw : Lines( ReadFile( file ) ) )
		{
			const std::string line = Trim( raw );
			if ( line.empty() || line[0] == '#' || !line.compare( 0, 2, "//" ) ) continue;
			std::istringstream fields( line );
			ManifestItem item;
			std::string extra;
			if ( !( fields >> item.source >> item.stage >> item.logical >> item.profile ) ) continue;
			fields >> item.legacy;
			if ( fields >> extra || fs::path( item.source ).extension() != ".fxc" ||
			     ( item.stage != "vs" && item.stage != "ps" && item.stage != "cs" ) ||
			     ( item.profile != "20b" && item.profile != "20" && item.profile != "30" && item.profile != "native" ) )
				continue; // nativeshaderpack validation reports malformed lines
			if ( item.profile == "native" ) item.legacy.clear();
			else if ( item.legacy.empty() || item.legacy == "-" ) item.legacy = item.logical + ".fxc";
			item.compiledBase = CompiledBase( item.source, item.stage );
			items.push_back( std::move( item ) );
		}
	}
	return items;
}

//-----------------------------------------------------------------------------
// Compile
//-----------------------------------------------------------------------------
struct ShaderWork
{
	const ManifestItem *item = nullptr;
	ComboLayout layout;
	const std::string *source = nullptr;
	fs::path sourcePath;
	struct Combo { uint32_t staticId, dynamicId; std::map<std::string, int> values; const std::string *program = nullptr; std::string programKey; };
	std::vector<Combo> combos;
	std::string error;
};

struct Options
{
	fs::path root, legacyRoot, destination, staging, cache;
	std::set<std::string> only;
	unsigned jobs = 0;
	bool verbose = false;
};

class Compiler
{
public:
	Compiler( const Options &options, const SourceTree &tree, const DxcApi &dxc ) : m_Options( options ), m_Tree( tree ), m_Dxc( dxc ) {}

	void Run( std::vector<ShaderWork> &work )
	{
		std::vector<std::pair<size_t, size_t>> tasks;
		for ( size_t s = 0; s < work.size(); ++s )
			for ( size_t c = 0; c < work[s].combos.size(); ++c ) tasks.push_back( { s, c } );
		// Interleave logicals so a heavy shader's combos spread over every worker.
		std::stable_sort( tasks.begin(), tasks.end(), []( const auto &a, const auto &b ) { return a.second < b.second; } );
		std::atomic<size_t> next{ 0 }, done{ 0 };
		const unsigned workers = std::max( 1u, m_Options.jobs ? m_Options.jobs : std::thread::hardware_concurrency() );
		std::vector<std::thread> threads;
		const auto start = std::chrono::steady_clock::now();
		for ( unsigned t = 0; t < workers; ++t )
			threads.emplace_back( [&]
			{
				ComPtr<IDxcUtils> utils;
				ComPtr<IDxcCompiler3> compiler;
				if ( FAILED( m_Dxc.create( CLSID_DxcUtils, IID_PPV_ARGS( &utils ) ) ) || FAILED( m_Dxc.create( CLSID_DxcCompiler, IID_PPV_ARGS( &compiler ) ) ) )
				{
					std::lock_guard<std::mutex> lock( m_Mutex );
					m_Fatal = "dxcompiler instance creation failed";
					return;
				}
				TreeIncludeHandler includes( m_Tree, utils.Get() );
				// An exception escaping a std::thread terminates the process (0xC0000409 with no message), so I/O and
				// allocation failures are recorded and reported by Run instead.
				try
				{
					for ( size_t i; ( i = next++ ) < tasks.size(); )
					{
						ShaderWork &shader = work[tasks[i].first];
						CompileCombo( utils.Get(), compiler.Get(), &includes, shader, shader.combos[tasks[i].second] );
						const size_t finished = ++done;
						if ( finished % 1000 == 0 || finished == tasks.size() )
						{
							const double seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
							std::lock_guard<std::mutex> lock( m_Mutex );
							std::cout << "ShaderCompiler3: " << finished << "/" << tasks.size() << " combos, " << m_Compiled << " compiled, "
							          << m_CacheHits << " cached, " << int( seconds ) << " s\n" << std::flush;
						}
					}
				}
				catch ( const std::exception &e )
				{
					std::lock_guard<std::mutex> lock( m_Mutex );
					if ( m_Fatal.empty() ) m_Fatal = e.what();
				}
			} );
		for ( auto &thread : threads ) thread.join();
		if ( !m_Fatal.empty() ) throw std::runtime_error( m_Fatal );
	}

private:
	void CompileCombo( IDxcUtils *utils, IDxcCompiler3 *compiler, TreeIncludeHandler *includes, ShaderWork &shader, ShaderWork::Combo &combo )
	{
		{
			std::lock_guard<std::mutex> lock( m_Mutex );
			if ( !shader.error.empty() ) return;
		}
		const std::string profile = shader.item->stage + "_6_0";
		std::vector<std::wstring> defines;
		for ( const auto &[name, value] : combo.values ) defines.push_back( Widen( name + "=" + std::to_string( value ) ) );
		// 1. Preprocess: the cache key is the expanded text, so an edit only rebuilds the combos it actually reaches.
		std::vector<LPCWSTR> args;
		const std::wstring sourceName = shader.sourcePath.wstring();
		args.push_back( sourceName.c_str() );
		args.push_back( L"-P" );
		args.push_back( L"-HV" ); args.push_back( L"2016" );
		for ( const auto &d : defines ) { args.push_back( L"-D" ); args.push_back( d.c_str() ); }
		const DxcBuffer source = { shader.source->data(), shader.source->size(), DXC_CP_UTF8 };
		ComPtr<IDxcResult> result;
		HRESULT status = E_FAIL;
		ComPtr<IDxcBlobUtf8> expanded;
		if ( FAILED( compiler->Compile( &source, args.data(), UINT32( args.size() ), includes, IID_PPV_ARGS( &result ) ) ) ||
		     FAILED( result->GetStatus( &status ) ) || FAILED( status ) ||
		     FAILED( result->GetOutput( DXC_OUT_HLSL, IID_PPV_ARGS( &expanded ), nullptr ) ) || !expanded )
		{
			Fail( shader, combo, "preprocess", result ? ResultErrors( result.Get() ) : std::string() );
			return;
		}
		const std::string text( expanded->GetStringPointer(), expanded->GetStringLength() );
		static const char kFlags[] = "-E main -O3 -HV 2016 -Gec";
		const std::string key = Sha256( m_Dxc.version + '\n' + profile + '\n' + kFlags + '\n' + text );
		const std::string *program = nullptr;
		{
			// Many combos expand to identical text. The first thread to reach a key loads or compiles it; the others
			// wait for that result instead of compiling the same program and racing on its cache file.
			std::unique_lock<std::mutex> lock( m_Mutex );
			m_ProgramReady.wait( lock, [&] { return !m_PendingPrograms.count( key ); } );
			auto it = m_Programs.find( key );
			if ( it != m_Programs.end() ) program = it->second.get();
			else m_PendingPrograms.insert( key );
		}
		if ( !program )
		{
			const ProgramClaim claim( *this, key );
			const fs::path cached = m_Options.cache / key.substr( 0, 2 ) / ( key + ".dxil" );
			if ( fs::exists( cached ) )
			{
				program = Store( key, ReadFile( cached ) );
				++m_CacheHits;
			}
			else
			{
				// 2. Compile the expanded text (its #line directives keep the original file names in diagnostics).
				const std::wstring wideProfile = Widen( profile );
				LPCWSTR compileArgs[] = { sourceName.c_str(), L"-T", wideProfile.c_str(), L"-E", L"main", L"-O3", L"-HV", L"2016", L"-Gec" };
				const DxcBuffer expandedSource = { text.data(), text.size(), DXC_CP_UTF8 };
				ComPtr<IDxcResult> compiled;
				ComPtr<IDxcBlob> object;
				status = E_FAIL;
				if ( FAILED( compiler->Compile( &expandedSource, compileArgs, UINT32( std::size( compileArgs ) ), nullptr, IID_PPV_ARGS( &compiled ) ) ) ||
				     FAILED( compiled->GetStatus( &status ) ) || FAILED( status ) ||
				     FAILED( compiled->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( &object ), nullptr ) ) || !object || !object->GetBufferSize() )
				{
					Fail( shader, combo, "compile", compiled ? ResultErrors( compiled.Get() ) : std::string() );
					return;
				}
				std::string bytes( static_cast<const char *>( object->GetBufferPointer() ), object->GetBufferSize() );
				// A signed container has a nonzero digest (bytes 4..19); the runtime rejects unsigned DXIL.
				bool signedContainer = false;
				for ( size_t i = 4; i < 20 && i < bytes.size(); ++i ) signedContainer |= bytes[i] != 0;
				if ( bytes.size() < 32 || memcmp( bytes.data(), "DXBC", 4 ) || !signedContainer )
				{
					Fail( shader, combo, "sign", "dxcompiler produced an unsigned or malformed container (is dxil.dll beside ShaderCompiler3.exe?)" );
					return;
				}
				WriteCacheFile( cached, bytes );
				program = Store( key, std::move( bytes ) );
				++m_Compiled;
			}
		}
		combo.program = program;
		combo.programKey = key;
	}

	const std::string *Store( const std::string &key, std::string bytes )
	{
		std::lock_guard<std::mutex> lock( m_Mutex );
		auto &slot = m_Programs[key];
		if ( !slot ) slot = std::make_unique<std::string>( std::move( bytes ) );
		return slot.get();
	}

	// Releases a claimed program key on every exit path (success, failure or exception) and wakes its waiters.
	class ProgramClaim
	{
	public:
		ProgramClaim( Compiler &compiler, const std::string &key ) : m_Compiler( compiler ), m_Key( key ) {}
		~ProgramClaim()
		{
			{
				std::lock_guard<std::mutex> lock( m_Compiler.m_Mutex );
				m_Compiler.m_PendingPrograms.erase( m_Key );
			}
			m_Compiler.m_ProgramReady.notify_all();
		}
	private:
		Compiler &m_Compiler;
		const std::string &m_Key;
	};

	void Fail( ShaderWork &shader, const ShaderWork::Combo &combo, const char *step, const std::string &errors )
	{
		std::lock_guard<std::mutex> lock( m_Mutex );
		if ( !shader.error.empty() ) return;
		std::ostringstream message;
		message << shader.item->logical << " (" << shader.item->source << ") static " << combo.staticId << " dynamic " << combo.dynamicId << " " << step << " failed:";
		for ( const auto &[name, value] : combo.values ) message << " " << name << "=" << value;
		message << "\n" << errors;
		shader.error = message.str();
	}

	const Options &m_Options;
	const SourceTree &m_Tree;
	const DxcApi &m_Dxc;
	std::mutex m_Mutex;
	std::unordered_map<std::string, std::unique_ptr<std::string>> m_Programs;
	std::unordered_set<std::string> m_PendingPrograms;	// keys a thread is loading or compiling right now
	std::condition_variable m_ProgramReady;
	std::atomic<size_t> m_Compiled{ 0 }, m_CacheHits{ 0 };
	std::string m_Fatal;
};

void Usage()
{
	throw std::runtime_error( "Usage: ShaderCompiler3 (-game <gamedir> | -stage <dir>) [-root <stdshaders_dx12>] [-legacyroot <stdshaders>] "
	                          "[-only <logical,logical,...>] [-jobs <n>] [-cache <dir>]" );
}

Options ParseOptions( int argc, char **argv )
{
	Options options;
	fs::path game, stage;
	for ( int i = 1; i < argc; ++i )
	{
		const std::string key = Lower( argv[i] );
		const auto value = [&]() -> std::string { if ( i + 1 >= argc ) Usage(); return argv[++i]; };
		if ( key == "-game" ) game = value();
		else if ( key == "-stage" ) stage = value();
		else if ( key == "-root" ) options.root = value();
		else if ( key == "-legacyroot" ) options.legacyRoot = value();
		else if ( key == "-only" )
		{
			std::istringstream names( value() );
			for ( std::string name; std::getline( names, name, ',' ); ) if ( !Trim( name ).empty() ) options.only.insert( Trim( name ) );
		}
		else if ( key == "-jobs" ) options.jobs = unsigned( std::stoul( value() ) );
		else if ( key == "-cache" ) options.cache = value();
		else if ( key == "-verbose" ) options.verbose = true;
		else Usage();
	}
	if ( game.empty() == stage.empty() ) Usage();
	options.destination = fs::absolute( game.empty() ? stage : game );
	if ( !fs::is_directory( options.destination ) ) throw std::runtime_error( "Destination does not exist: " + options.destination.string() );
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW( nullptr, exe, MAX_PATH );
	const fs::path src = fs::path( exe ).parent_path().parent_path().parent_path().parent_path(); // src\devtools\bin\x64
	if ( options.root.empty() ) options.root = src / "materialsystem" / "stdshaders_dx12";
	if ( options.legacyRoot.empty() ) options.legacyRoot = src / "materialsystem" / "stdshaders";
	options.root = fs::canonical( options.root );
	options.legacyRoot = fs::canonical( options.legacyRoot );
	// Per-tree staging and cache, keyed on the source root (several checkouts never share artifacts).
	const std::string rootKey = Sha256( Lower( options.root.string() ) ).substr( 0, 16 );
	const fs::path temp = fs::temp_directory_path();
	options.staging = temp / ( "shadercompiler3-" + rootKey );
	if ( options.cache.empty() ) options.cache = temp / ( "shadercompiler3-cache-" + rootKey );
	return options;
}

void Run( const Options &options )
{
	const auto start = std::chrono::steady_clock::now();
	// Native tree: hlsl\ and native_src\ flattened by file name, plus the shared projected-light limits header.
	SourceTree tree;
	for ( const char *dir : { "hlsl", "native_src" } )
		if ( fs::exists( options.root / dir ) )
			for ( const auto &entry : fs::recursive_directory_iterator( options.root / dir ) )
				if ( entry.is_regular_file() ) tree.Add( entry.path() );
	tree.Add( options.root.parent_path().parent_path() / "public" / "shaderapi" / "projected_light_limits.h" );
	SourceTree legacyTree;
	for ( const auto &entry : fs::directory_iterator( options.legacyRoot ) )
		if ( entry.is_regular_file() && ( entry.path().extension() == ".fxc" || entry.path().extension() == ".h" ) ) legacyTree.Add( entry.path() );

	const std::vector<ManifestItem> items = ReadManifests( options.root );
	std::set<std::string> logicals;
	for ( const auto &item : items ) logicals.insert( item.logical );
	for ( const auto &name : options.only )
		if ( !logicals.count( name ) ) throw std::runtime_error( "Unknown logical in -only: " + name );

	// Legacy combo-ABI references (DX9 sources, legacy_reference overrides replace only the main file).
	for ( const auto &item : items )
	{
		if ( item.profile == "native" ) continue;
		const fs::path override = options.root / "legacy_reference" / item.legacy;
		const auto *original = legacyTree.Find( item.legacy );
		if ( !fs::exists( override ) && !original ) throw std::runtime_error( "Missing legacy shader: " + ( options.legacyRoot / item.legacy ).string() );
		const std::string text = fs::exists( override ) ? ReadFile( override ) : original->second;
		std::vector<Directive> directives;
		CollectDirectives( legacyTree, text, directives );
		const std::string token = item.profile == "30" ? "30" : item.stage == "vs" ? "20" : "20b";
		const std::string base = LegacyBase( item.legacy, item.stage, item.profile );
		const ComboLayout layout = BuildLayout( FilterLegacy( directives, item.stage + token ) );
		WriteFile( options.staging / "legacy" / item.profile / "include" / ( base + ".inc" ), ComboInclude( base, item.stage, layout ) );
	}

	std::vector<ShaderWork> work;
	std::string nativeMap;
	for ( const auto &item : items )
	{
		const fs::path artifact = fs::path( "native" ) / item.logical;
		const fs::path inc = options.staging / artifact / "include" / ( item.compiledBase + ".inc" );
		const fs::path vcs = options.staging / artifact / "shaders" / "fxc" / ( item.compiledBase + ".vcs" );
		nativeMap += item.source + "|" + item.stage + "|" + item.logical + "|" + item.compiledBase + "|" + artifact.string() + "\n";
		if ( !options.only.empty() && !options.only.count( item.logical ) )
		{
			if ( !fs::exists( inc ) || !fs::exists( vcs ) )
				throw std::runtime_error( "No staged artifacts for " + item.logical + "; run ShaderCompiler3 without -only first" );
			continue;
		}
		const auto *file = tree.Find( item.source );
		if ( !file ) throw std::runtime_error( "Missing native source: " + item.source );
		ShaderWork shader;
		shader.item = &item;
		shader.source = &file->second;
		shader.sourcePath = file->first;
		std::vector<Directive> directives;
		CollectDirectives( tree, file->second, directives );
		shader.layout = BuildLayout( directives );
		std::map<std::string, int> values;
		for ( uint64_t s = 0; s < shader.layout.StaticCount(); ++s )
			for ( uint64_t d = 0; d < shader.layout.DynamicCount(); ++d )
			{
				shader.layout.Values( s, d, values );
				if ( !shader.layout.Skipped( values ) ) shader.combos.push_back( { uint32_t( s ), uint32_t( d ), values, nullptr } );
			}
		work.push_back( std::move( shader ) );
	}
	WriteFile( options.staging / "native-map.txt", nativeMap );

	const DxcApi dxc = LoadDxc();
	std::cout << "ShaderCompiler3: dxcompiler " << dxc.version << ", " << work.size() << " logicals to build, staging " << options.staging.string() << "\n";
	Compiler compiler( options, tree, dxc );
	compiler.Run( work );

	size_t failures = 0;
	for ( const ShaderWork &shader : work )
		if ( !shader.error.empty() ) { std::cerr << shader.error << "\n"; ++failures; }
	if ( failures ) throw std::runtime_error( std::to_string( failures ) + " logical(s) failed to compile" );
	// A staged VCS is rewritten only when its combo set or a program changed (its .key holds the hash of both); the
	// LZMA encoding of the rest runs on every core.
	std::atomic<size_t> nextWrite{ 0 }, written{ 0 };
	std::string writeError;
	std::mutex writeMutex;
	std::vector<std::thread> writers;
	for ( unsigned t = 0, n = std::max( 1u, options.jobs ? options.jobs : std::thread::hardware_concurrency() ); t < n; ++t )
		writers.emplace_back( [&]
		{
			for ( size_t i; ( i = nextWrite++ ) < work.size(); )
			{
				try
				{
					const ShaderWork &shader = work[i];
					std::map<uint32_t, std::vector<std::pair<uint32_t, const std::string *>>> programs;
					std::string identity = shader.item->compiledBase + "\n" + std::to_string( shader.layout.centroidMask ) + "\n";
					for ( const auto &combo : shader.combos )
					{
						programs[combo.staticId].push_back( { combo.dynamicId, combo.program } );
						identity += std::to_string( combo.staticId ) + ":" + std::to_string( combo.dynamicId ) + ":" + combo.programKey + "\n";
					}
					// VCS header CRC field: the main source's SHA-256 prefix (no consumer reads it; it identifies the build input).
					const std::string sourceDigest = Sha256( *shader.source );
					const uint32_t sourceHash = uint32_t( std::stoul( sourceDigest.substr( 0, 8 ), nullptr, 16 ) );
					identity += sourceDigest + "\nlzma1\n";
					const fs::path artifact = options.staging / "native" / shader.item->logical;
					WriteFile( artifact / "include" / ( shader.item->compiledBase + ".inc" ), ComboInclude( shader.item->compiledBase, shader.item->stage, shader.layout ) );
					const fs::path vcs = artifact / "shaders" / "fxc" / ( shader.item->compiledBase + ".vcs" );
					const fs::path keyFile = vcs.string() + ".key";
					const std::string key = Sha256( identity );
					if ( fs::exists( vcs ) && fs::exists( keyFile ) && ReadFile( keyFile ) == key ) continue;
					WriteFile( vcs, WriteVcs( shader.layout, programs, sourceHash ) );
					WriteFile( keyFile, key );
					++written;
				}
				catch ( const std::exception &e )
				{
					std::lock_guard<std::mutex> lock( writeMutex );
					if ( writeError.empty() ) writeError = e.what();
				}
			}
		} );
	for ( auto &thread : writers ) thread.join();
	if ( !writeError.empty() ) throw std::runtime_error( writeError );
	std::cout << "ShaderCompiler3: " << written << " of " << work.size() << " VCS files rewritten\n";
	// Logicals no longer in any manifest are dropped from the staging tree.
	if ( fs::exists( options.staging / "native" ) )
		for ( const auto &entry : fs::directory_iterator( options.staging / "native" ) )
			if ( entry.is_directory() && !logicals.count( entry.path().filename().string() ) ) fs::remove_all( entry.path() );
	sc3::PackAndPublish( options.root, options.staging, options.destination );
	std::cout << "ShaderCompiler3: done in " << int( std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count() ) << " s\n";
}

} // namespace

int main( int argc, char **argv )
{
	try
	{
		Run( ParseOptions( argc, argv ) );
		return 0;
	}
	catch ( const std::exception &e )
	{
		std::cerr << "ShaderCompiler3: " << e.what() << '\n';
		return 1;
	}
}
