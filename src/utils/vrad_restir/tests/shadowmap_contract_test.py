#!/usr/bin/env python3
"""Real BSP contract regressions. Preparation is safe; execution requires built tools.

No third-party Python packages, mocked bakes, or runtime-gameinfo edits. The smoke
host's -report JSON is supplied separately with --runtime-report. Missing GPU
readback evidence is a failure, never a skipped/passing runtime scenario.

Run --prepare-only first. Then --only receiver-inputs creates receivers.json
from real zero-bounce LDR/HDR controls. Feed that to the smoke host's
-receivers <json> -report <json>. Capture game r_shadowmap_report files after
ordinary/fast detail initialization and a 4097-client/static-renderable view.
Pass each output with a repeated --runtime-report to the full run.
The hook-free v3 sidecar carries full selected worldlights and independently
traced geometric sun visibility. Feature conversion always writes both modes.
Baked-mask assertions complement, rather than replace, runtime GPU readbacks.
PPL coverage follows compiled static-prop texel-lighting admission; preparation
prefers a mountable model declaring $lightmapres. Otherwise VHV is required.
Zero-bounce prop/detail gathers retain full-source lightmap indirect; selected
direct removal is proved by ordinary-minus-feature receiver deltas. Ambient
cube payloads are unchanged reflected gathers of the full-source lightmap.
"""
from __future__ import annotations

import argparse
import io
import json
import lzma
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

SRC = Path(__file__).resolve().parents[3]
GAME = SRC.parent / "game"
INVALID = "Shadowmaps: invalid BSP lighting metadata"
INVALID_SIZE = "Shadowmaps: invalid shadow emitter size"
MODE_LUMPS = ((8, 7, 15, 52, 56), (53, 58, 54, 51, 55))
MODE_KEYS = ("lightingBytes", "lightingCRC32", "facesCRC32", "worldlightsCRC32",
             "lightsOffset", "lightCount", "sunLightIndex", "reserved",
             "receiverFacesOffset", "receiverFaceCount", "receiverTrianglesOffset",
             "receiverTriangleCount", "sunVisibilityOffset", "sunVisibilityCount",
             "receiverDataCRC32", "reserved2")
SHADOW_LIGHT_STRIDE = 116
SHADOW_HEADER_SIZE = 144
SHADOW_MODE_SIZE = 64
SHADOW_FACE_STRIDE = 96
SHADOW_TRIANGLE_STRIDE = 60
RECEIVER_KEYS = ("dfaceIndex", "modelIndex", "lightingOffset", "firstSunVisibility",
                 "luxelW", "luxelH", "numChannels", "numStyles", "flags",
                 "firstTriangle", "triangleCount", "reserved")
SCENARIOS = (
    "format", "receiver-inputs", "runtime-calibration", "selected-zero-bounce", "indirect-corner",
    "full-transport", "default-point", "opted-point", "wide-spot", "opt-out",
    "worldlight-omission", "implied-both", "rescale-both",
    "sun-visibility", "sun-zero-intensity", "receiver-layout",
    "receiver-gathers", "backend-equivalence", "ordinary-reconversion",
    "malformed-counts", "malformed-offsets", "malformed-crcs", "unknown-version",
    "malformed-overlap", "malformed-receivers", "malformed-padding",
    "emitter-defaults", "emitter-zero",
    "emitter-invalid", "multi-env-last-wins", "emitter-transport", "overflow-72-spots",
    "detail-initialization", "casters-4097", "pcss-radii", "pcss-contact-hardening",
    "pcss-depth", "pcss-residency",
)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def unpack(fmt, data, offset=0):
    size = struct.calcsize(fmt)
    require(0 <= offset <= len(data) - size, f"truncated {fmt} at {offset}")
    return struct.unpack_from(fmt, data, offset)


def rgbexp(data):
    r, g, b, e = unpack("<BBBb", data)
    return tuple(math.ldexp(c / 255.0, e) for c in (r, g, b))


def quantum(data):
    return math.ldexp(1.0 / 255.0, unpack("<BBBb", data)[3])


def close_rgb(a, b, qa=0.0, qb=0.0, label="RGB"):
    for c, (x, y) in enumerate(zip(a, b)):
        require(math.isfinite(x) and math.isfinite(y), f"{label}: nonfinite")
        require(abs(x - y) <= 0.02 * max(abs(x), abs(y)) + qa + qb + 1e-7,
                f"{label}[{c}]: {x} != {y} (2% + quantization)")


def valve_lzma(data):
    require(data[:4] == b"LZMA", "unknown BSP compression")
    actual, packed = unpack("<II", data, 4)
    props = data[12]
    lc, rest = props % 9, props // 9
    lp, pb = rest % 5, rest // 5
    dictionary = unpack("<I", data, 13)[0]
    raw = lzma.decompress(data[17:17 + packed], format=lzma.FORMAT_RAW,
                          filters=[dict(id=lzma.FILTER_LZMA1, dict_size=dictionary,
                                        lc=lc, lp=lp, pb=pb)])
    require(len(raw) == actual, "LZMA length mismatch")
    return raw


def worldlight_record(raw, offset=0):
    f = unpack("<9f3i7f3i", raw, offset)
    return dict(origin=f[:3], intensity=f[3:6], normal=f[6:9], cluster=f[9],
                type=f[10], style=f[11], stopdot=f[12], stopdot2=f[13],
                exponent=f[14], radius=f[15], constant_attn=f[16], linear_attn=f[17],
                quadratic_attn=f[18], flags=f[19], texinfo=f[20], owner=f[21])


def same_light(a,b):
    if a["type"]!=b["type"] or a["style"]!=b["style"]:
        return False
    return all(abs(x-y)<=1e-6*max(1,abs(x),abs(y))
               for field in ("origin","normal","intensity") for x,y in zip(a[field],b[field]))


class BSP:
    """Little-endian dheader_t, dgamelump_t, dface_t and dworldlight_t reader."""
    def __init__(self, path):
        self.path = Path(path)
        self.raw = self.path.read_bytes()
        require(self.raw[:4] == b"VBSP", f"not a BSP: {path}")
        self.version = unpack("<I", self.raw, 4)[0]
        require(self.version in (20, 21), f"unsupported BSP version {self.version}")
        self.directory = [unpack("<4i", self.raw, 8 + i * 16) for i in range(64)]
        self.lumps = []
        for offset, length, version, expanded in self.directory:
            require(offset >= 0 and length >= 0 and offset + length <= len(self.raw), "BSP lump range")
            payload = self.raw[offset:offset + length]
            self.lumps.append(valve_lzma(payload) if expanded else payload)
        self.flags = unpack("<I", self.lumps[59])[0] if self.lumps[59] else 0
        self.games = {}
        game = self.lumps[35]
        if game:
            count = unpack("<i", game)[0]
            require(0 <= count <= (len(game) - 4) // 16, "game lump count")
            entries = [unpack("<IHHii", game, 4 + i * 16) for i in range(count)]
            for i, (identifier, flags, version, offset, length) in enumerate(entries):
                if not identifier:
                    continue
                name = identifier.to_bytes(4, "big").decode("ascii")
                require(name not in self.games, "duplicate game lump")
                end = entries[i + 1][3] if flags & 1 and i + 1 < count else offset + length
                require(0 <= offset <= end <= len(self.raw), "game payload range")
                payload = self.raw[offset:end]
                if flags & 1:
                    payload = valve_lzma(payload)
                require(len(payload) == length, "game payload size")
                self.games[name] = dict(version=version, flags=flags, data=payload,
                                        offset=offset, directoryOffset=self.directory[35][0] + 4 + i * 16)
        self.pak = {}
        if self.lumps[40]:
            with zipfile.ZipFile(io.BytesIO(self.lumps[40])) as archive:
                for info in archive.infolist():
                    raw = archive.read(info)  # zipfile independently checks the ZIP CRC.
                    require(crc(raw) == info.CRC, f"pak CRC {info.filename}")
                    self.pak[info.filename.lower()] = raw
        self.shadow = self.parse_shadow()

    def game(self, name):
        return self.games.get(name, {}).get("data", b"")

    def worldlights(self, mode):
        raw = self.lumps[MODE_LUMPS[mode][2]]
        require(len(raw) % 88 == 0, "dworldlight_t stride")
        return [worldlight_record(raw,at) for at in range(0,len(raw),88)]

    def faces(self, mode):
        raw = self.lumps[MODE_LUMPS[mode][1]]
        require(len(raw) % 56 == 0, "dface_t stride")
        for at in range(0, len(raw), 56):
            plane, side, onnode, edge, edges, texinfo, disp, fog = unpack("<HBBihhhh", raw, at)
            styles = tuple(x for x in raw[at + 16:at + 20] if x != 255)
            offset, area, minx, miny, sizex, sizey = unpack("<if4i", raw, at + 20)
            flags = unpack("<i", self.lumps[6], texinfo * 72 + 64)[0] if texinfo >= 0 else 0
            yield dict(index=at // 56, plane=plane, side=side, edge=edge, edges=edges,
                       texinfo=texinfo, disp=disp, styles=styles, offset=offset,
                       mins=(minx, miny), size=(sizex + 1, sizey + 1),
                       channels=4 if flags & 0x800 else 1)

    def detail(self):
        raw = self.game("dprp")
        if not raw:
            return 0, []
        count = unpack("<i", raw)[0]
        at = 4 + count * 128
        count = unpack("<i", raw, at)[0]
        at += 4 + count * 32
        count = unpack("<i", raw, at)[0]
        at += 4
        require(count >= 0 and at + count * 52 == len(raw), "dprp stride/count")
        records = [raw[at + i * 52:at + (i + 1) * 52] for i in range(count)]
        return at, records

    def static_dictionary(self):
        raw = self.game("sprp")
        if not raw:
            return [], 0
        count = unpack("<i", raw)[0]
        require(0 <= count <= (len(raw) - 4) // 128, "sprp dictionary")
        names = [raw[4 + i * 128:4 + (i + 1) * 128].split(b"\0")[0].decode("ascii") for i in range(count)]
        at = 4 + count * 128
        leaves = unpack("<i", raw, at)[0]
        at += 4 + leaves * 2
        count = unpack("<i", raw, at)[0]
        require(count >= 0, "sprp count")
        return names, count

    def texel_props(self):
        raw = self.game("sprp")
        if not raw:
            return []
        version = self.games["sprp"]["version"]
        require(version in (4,5,6,10), "unsupported sprp version")
        if version != 10:  # Legacy records have no per-texel lighting.
            return []
        names,count = self.static_dictionary()
        at = 4 + len(names)*128
        leaves = unpack("<i",raw,at)[0]
        require(leaves>=0, "sprp leaf count")
        at += 8 + leaves*2
        require(at+count*72==len(raw), "sprp v10 stride/count")
        props = []
        for prop in range(count):
            flags,width,height = unpack("<IHH",raw,at+prop*72+64)
            if not flags&0x100:
                require(width>0 and height>0, "texel-lit prop lacks resolution")
                props.append(prop)
        return props

    def parse_shadow(self):
        raw = self.game("rshd")
        if not raw:
            return None
        require(self.games["rshd"]["version"]==3,"unknown rshd version")
        size,runtime,reserved0,reserved1 = unpack("<4I",raw)
        require(size==len(raw) and size>=SHADOW_HEADER_SIZE and size%4==0,"rshd byteSize")
        require(runtime and not runtime&~3 and reserved0==reserved1==0,"rshd header")
        modes = []
        for mode in range(2):
            values = unpack("<6Ii9I",raw,16+mode*SHADOW_MODE_SIZE)
            rec = dict(zip(MODE_KEYS,values))
            for offset_key,count_key,stride in (
                    ("lightsOffset","lightCount",SHADOW_LIGHT_STRIDE),
                    ("receiverFacesOffset","receiverFaceCount",SHADOW_FACE_STRIDE),
                    ("receiverTrianglesOffset","receiverTriangleCount",SHADOW_TRIANGLE_STRIDE),
                    ("sunVisibilityOffset","sunVisibilityCount",1)):
                off,count = rec[offset_key],rec[count_key]
                padded = (count*stride+3)&~3
                require((count==0 and off==0) or
                        (count>0 and off>=SHADOW_HEADER_SIZE and off%4==0 and off+padded<=len(raw)),
                        offset_key+" section range")
            off,count = rec["lightsOffset"],rec["lightCount"]
            rec["lights"] = []
            for index in range(count):
                at = off+index*SHADOW_LIGHT_STRIDE
                source,angular,radius,start,end,cap,reserved = unpack("<i5fI",raw,at+88)
                rec["lights"].append(dict(light=worldlight_record(raw,at),sourceEntity=source,
                                          shadowSunAngularRadius=angular,shadowSourceRadius=radius,
                                          startFade=start,endFade=end,capDist=cap,reserved=reserved))
            rec["receiverFaces"] = []
            for index in range(rec["receiverFaceCount"]):
                at = rec["receiverFacesOffset"]+index*SHADOW_FACE_STRIDE
                receiver = dict(zip(RECEIVER_KEYS,unpack("<12I",raw,at)))
                receiver["worldToLuxel"] = unpack("<8f",raw,at+48)
                receiver["plane"] = unpack("<4f",raw,at+80)
                rec["receiverFaces"].append(receiver)
            rec["receiverTriangles"] = []
            for index in range(rec["receiverTriangleCount"]):
                values = unpack("<15f",raw,rec["receiverTrianglesOffset"]+index*SHADOW_TRIANGLE_STRIDE)
                rec["receiverTriangles"].append(dict(
                    position=[values[i:i+3] for i in range(0,9,3)],
                    luxel=[values[i:i+2] for i in range(9,15,2)]))
            off,count = rec["sunVisibilityOffset"],rec["sunVisibilityCount"]
            rec["sunVisibility"] = raw[off:off+count] if count else b""
            rec["runtime"] = bool(runtime&(1<<mode))
            modes.append(rec)
        return dict(runtime=runtime,modes=modes)

    def validate(self):
        if not self.shadow:
            require(not self.flags&0xc0000,"orphan runtime level flags")
            return
        ranges = []
        for mode,rec in enumerate(self.shadow["modes"]):
            require(rec["reserved"]==rec["reserved2"]==0,"mode reserved")
            if not rec["runtime"]:
                require(all(rec[k]==(-1 if k=="sunLightIndex" else 0) for k in MODE_KEYS),"absent mode data")
                require(not self.flags&(0x40000<<mode),"absent mode flags")
                continue
            light,face,world,_,_ = MODE_LUMPS[mode]
            require(rec["lightingBytes"]==len(self.lumps[light]) and self.lumps[face],"converted mode lumps missing")
            for key,payload in (("lightingCRC32",self.lumps[light]),("facesCRC32",self.lumps[face]),("worldlightsCRC32",self.lumps[world])):
                require(rec[key]==crc(payload),key)
            require(rec["lights"] and self.flags&(0x40000<<mode),"runtime mode missing light list/flag")
            sun = rec["sunLightIndex"]
            require(sun in (-1,0) and sun<len(rec["lights"]),"sun must be first")
            ordinary = self.worldlights(mode)
            for index,record in enumerate(rec["lights"]):
                wl = record["light"]
                require(record["reserved"]==0,"light reserved")
                scalars = [*wl["origin"],*wl["intensity"],*wl["normal"],wl["constant_attn"],wl["linear_attn"],wl["quadratic_attn"],wl["stopdot"],wl["stopdot2"],wl["exponent"],wl["radius"],record["shadowSunAngularRadius"],record["shadowSourceRadius"],record["startFade"],record["endFade"],record["capDist"]]
                require(all(math.isfinite(x) for x in scalars),"light record not finite")
                require(record["startFade"]>=0 and record["capDist"]>=0,"light fade")  # endFade < 0 (VRAD default -1) = no fade
                if index==sun:
                    require(wl["type"]==3 and 0<=record["shadowSunAngularRadius"]<90 and record["shadowSourceRadius"]==0,"sun record")
                    require(record["startFade"]==record["endFade"]==record["capDist"]==0,"sun fade")
                else:
                    require(wl["type"] in (1,2) and record["shadowSunAngularRadius"]==0 and record["shadowSourceRadius"]>=0,"local record")
                    require(record["capDist"]>0,"local cap distance")
                require(not any(same_light(wl,w) for w in ordinary),"selected light still in ordinary worldlights")
            sections = []
            for offset_key,count_key,stride in (
                    ("lightsOffset","lightCount",SHADOW_LIGHT_STRIDE),
                    ("receiverFacesOffset","receiverFaceCount",SHADOW_FACE_STRIDE),
                    ("receiverTrianglesOffset","receiverTriangleCount",SHADOW_TRIANGLE_STRIDE),
                    ("sunVisibilityOffset","sunVisibilityCount",1)):
                if rec[count_key]:
                    off = rec[offset_key]
                    end = off+rec[count_key]*stride
                    padded = (end+3)&~3
                    require(not any(self.game("rshd")[end:padded]),"nonzero scalar padding")
                    sections.append((off,padded))
            ranges.extend(sections)
            if sun<0:
                require(all(rec[key]==0 for key in MODE_KEYS[8:]),"orphan no-sun receiver data")
                continue
            require(rec["receiverFaces"] and rec["receiverTriangles"] and rec["sunVisibility"],
                    "selected sun missing receiver sections")
            receiver_bytes = b"".join(self.game("rshd")[rec[key]:rec[key]+rec[count]*stride]
                                      for key,count,stride in (
                                          ("receiverFacesOffset","receiverFaceCount",SHADOW_FACE_STRIDE),
                                          ("receiverTrianglesOffset","receiverTriangleCount",SHADOW_TRIANGLE_STRIDE),
                                          ("sunVisibilityOffset","sunVisibilityCount",1)))
            require(crc(receiver_bytes)==rec["receiverDataCRC32"],"receiver data CRC")
            faces = list(self.faces(mode))
            require(len(self.lumps[14])%48==0,"dmodel_t stride")
            allowed = {0,255} if rec["lights"][sun]["shadowSunAngularRadius"]==0 else {
                int(n*255/32+.5) for n in range(33)}
            require(set(rec["sunVisibility"])<=allowed,"sun scalar values are not prescribed ray fractions")
            models = [unpack("<2i",self.lumps[14],at+40) for at in range(0,len(self.lumps[14]),48)]
            scalar_prefix = triangle_prefix = 0
            previous = -1
            for receiver in rec["receiverFaces"]:
                index = receiver["dfaceIndex"]
                require(previous<index<len(faces),"receiver face identity/order")
                previous = index
                face = faces[index]
                require(receiver["reserved"]==0 and not receiver["flags"]&~3,"receiver reserved/flags")
                require(receiver["modelIndex"]<len(models),"receiver model index")
                first,count = models[receiver["modelIndex"]]
                require(first<=index<first+count,"receiver model ownership")
                require(receiver["lightingOffset"]==face["offset"] and face["offset"]>=0,"receiver lighting offset")
                require((receiver["luxelW"],receiver["luxelH"])==face["size"] and min(face["size"])>0,
                        "receiver dimensions")
                require(receiver["numChannels"]==face["channels"] and receiver["numStyles"]==len(face["styles"])>0,
                        "receiver styles/channels")
                require(bool(receiver["flags"]&1)==(face["channels"]==4) and
                        bool(receiver["flags"]&2)==(face["disp"]>=0),"receiver bump/displacement flags")
                count = receiver["luxelW"]*receiver["luxelH"]
                require(receiver["firstSunVisibility"]==scalar_prefix,"receiver scalar prefix")
                scalar_prefix += count
                require(scalar_prefix<=rec["sunVisibilityCount"],"receiver scalar count")
                lighting_end = face["offset"]+count*face["channels"]*len(face["styles"])*4
                require(face["offset"]%4==0 and lighting_end<=rec["lightingBytes"] and
                        face["offset"]>=4*len(face["styles"]),"receiver lighting bounds/prefix")
                require(receiver["firstTriangle"]==triangle_prefix and receiver["triangleCount"]>0,
                        "receiver triangle prefix")
                triangle_prefix += receiver["triangleCount"]
                require(triangle_prefix<=rec["receiverTriangleCount"],"receiver triangle count")
                values = receiver["worldToLuxel"]+receiver["plane"]
                require(all(math.isfinite(x) for x in values),"receiver nonfinite geometry")
                plane = unpack("<4f",self.lumps[1],face["plane"]*20)
                # Source stores the oriented plane index; side describes its relation to the node plane.
                oriented = plane
                require(all(abs(x-y)<=1e-5*max(1,abs(y)) for x,y in zip(receiver["plane"],oriented)),
                        "receiver oriented plane")
                vectors = list(unpack("<8f",self.lumps[6],face["texinfo"]*72+32))
                for axis in range(2):
                    vectors[axis*4+3] -= face["mins"][axis]
                require(all(abs(x-y)<=1e-5*max(1,abs(y)) for x,y in zip(receiver["worldToLuxel"],vectors)),
                        "receiver affine luxel transform")
                source_positions = set()
                if not receiver["flags"]&2 and receiver["modelIndex"]==0:
                    for edge in range(face["edge"],face["edge"]+face["edges"]):
                        surfedge = unpack("<i",self.lumps[13],edge*4)[0]
                        vertex = unpack("<2H",self.lumps[12],abs(surfedge)*4)[int(surfedge<0)]
                        source_positions.add(unpack("<3f",self.lumps[3],vertex*12))
                for triangle in rec["receiverTriangles"][receiver["firstTriangle"]:triangle_prefix]:
                    require(all(math.isfinite(x) for vertex in triangle["position"]+triangle["luxel"] for x in vertex),
                            "receiver triangle not finite")
                    require(all(-.01<=uv[a]<=face["size"][a]-1+.01 for uv in triangle["luxel"] for a in range(2)),
                            "receiver triangle luxel bounds")
                    if not receiver["flags"]&2:
                        for position,luxel in zip(triangle["position"],triangle["luxel"]):
                            if receiver["modelIndex"]==0:
                                require(position in source_positions,
                                        "regular receiver triangle is not unpushed source geometry")
                            for axis in range(2):
                                expected = sum(vectors[axis*4+k]*position[k] for k in range(3))+vectors[axis*4+3]
                                require(abs(luxel[axis]-expected)<=.01,"regular receiver triangle luxel label")
            require(scalar_prefix==rec["sunVisibilityCount"] and triangle_prefix==rec["receiverTriangleCount"],
                    "receiver final prefixes")
            require({r["dfaceIndex"] for r in rec["receiverFaces"]}==
                    {f["index"] for f in faces if f["offset"]>=0},"receiver coverage incomplete")
        canonical = SHADOW_HEADER_SIZE
        for start,end in ranges:
            require(start==canonical,"noncanonical/overlapping rshd sections")
            canonical = end
        require(canonical==len(self.game("rshd")),"orphan rshd bytes")

    def sun_sample(self, mode, position):
        """Sample the real scalar section using authenticated triangle UV labels."""
        require(self.shadow is not None,"sun sample without sidecar")
        rec = self.shadow["modes"][mode]
        candidates = []
        for face in rec["receiverFaces"]:
            for triangle in rec["receiverTriangles"][face["firstTriangle"]:
                                                       face["firstTriangle"]+face["triangleCount"]]:
                a,b,c = triangle["position"]
                u = [b[k]-a[k] for k in range(3)]
                v = [c[k]-a[k] for k in range(3)]
                p = [position[k]-a[k] for k in range(3)]
                dot = lambda x,y:sum(t*s for t,s in zip(x,y))
                uu,uv,vv = dot(u,u),dot(u,v),dot(v,v)
                determinant = uu*vv-uv*uv
                if determinant<=1e-12:
                    continue
                wb = (dot(p,u)*vv-dot(p,v)*uv)/determinant
                wc = (dot(p,v)*uu-dot(p,u)*uv)/determinant
                weights = (1-wb-wc,wb,wc)
                if min(weights)<-1e-5 or sum((p[k]-wb*u[k]-wc*v[k])**2 for k in range(3))>.01**2:
                    continue
                xy = [sum(weights[i]*triangle["luxel"][i][axis] for i in range(3)) for axis in range(2)]
                require(all(-.01<=xy[axis]<=face[("luxelW","luxelH")[axis]]-1+.01 for axis in range(2)),
                        "sun sample outside luxel plane")
                x,y = [max(0,min(face[key]-1,round(xy[axis])))
                       for axis,key in enumerate(("luxelW","luxelH"))]
                index = face["firstSunVisibility"]+x+y*face["luxelW"]
                candidates.append((rec["sunVisibility"][index],face["dfaceIndex"],index))
        require(candidates,f"no sun receiver triangle at {position}")
        require(len({value for value,_,_ in candidates})==1,f"ambiguous sun sample at {position}")
        value,face,index = candidates[0]
        return value,face,index

    def patch(self, path, patches):
        """Corruption copies only; never modifies the original fixture or integration map."""
        data = bytearray(self.raw)
        for offset, replacement in patches:
            require(0 <= offset <= len(data) - len(replacement), "patch range")
            data[offset:offset + len(replacement)] = replacement
        Path(path).write_bytes(data)
        return Path(path)


    def samples(self, mode, position, style=0, channel=0):
        """Read real RGBExp32 receiver plane using lightmap vectors and face bounds."""
        candidates = []
        for face in self.faces(mode):
            if face["offset"] < 0 or style not in face["styles"] or face["texinfo"] < 0:
                continue
            normal = unpack("<4f", self.lumps[1], face["plane"] * 20)
            distance = abs(sum(normal[k] * position[k] for k in range(3)) - normal[3])
            if distance > 2.0:
                continue
            vectors = unpack("<8f", self.lumps[6], face["texinfo"] * 72 + 32)
            xy = [sum(vectors[a * 4 + k] * position[k] for k in range(3)) + vectors[a * 4 + 3] - face["mins"][a] for a in range(2)]
            if any(xy[a] < -0.01 or xy[a] > face["size"][a] - 0.99 for a in range(2)):
                continue
            require(channel < face["channels"], "receiver channel")
            x, y = [max(0, min(face["size"][a] - 1, round(xy[a]))) for a in range(2)]
            count = math.prod(face["size"])
            slot = face["styles"].index(style)
            index = face["offset"] // 4 + (slot * face["channels"] + channel) * count + y * face["size"][0] + x
            raw = self.lumps[MODE_LUMPS[mode][0]][index * 4:index * 4 + 4]
            candidates.append((distance, face["index"], rgbexp(raw), quantum(raw)))
        require(candidates, f"no lightmapped face at {position}, style {style}")
        return min(candidates) [2:]

    def ambient_at(self, mode, position):
        node = 0
        steps = 0
        while node >= 0:
            require(steps <= len(self.lumps[5]) // 32, "BSP node cycle")
            plane, front, back = unpack("<3i", self.lumps[5], node * 32)
            normal = unpack("<4f", self.lumps[1], plane * 20)
            node = front if sum(normal[k]*position[k] for k in range(3)) >= normal[3] else back
            steps += 1
        leaf = -node-1
        require(self.directory[10][2] == 1, "fixture requires dleaf_t version 1")
        bounds = unpack("<6h", self.lumps[10], leaf*32+8)
        count, first = unpack("<HH", self.lumps[MODE_LUMPS[mode][3]], leaf*4)
        candidates = []
        raw = self.lumps[MODE_LUMPS[mode][4]]
        for index in range(first, first+count):
            sample = raw[index*28:(index+1)*28]
            require(len(sample) == 28, "ambient sample range")
            xyz = [bounds[k]+sample[24+k]/255*(bounds[k+3]-bounds[k]) for k in range(3)]
            distance = sum((position[k]-xyz[k])**2 for k in range(3))
            candidates.append((distance,index,sample[:24]))
        require(candidates, f"no real ambient cube at {position}")
        sample = min(candidates)[2]
        return [sample[k:k+4] for k in range(0,24,4)]

    def lookup_ppl(self, prop):
        name = f"texelslighting_{prop}.ppl"
        require(name in self.pak, f"actual PPL lookup failed: {name}")
        return self.pak[name]



class KV:
    """Minimal ordered Valve KeyValues tree; duplicate entities/keys are retained."""
    def __init__(self, items):
        self.items = items

    @classmethod
    def parse(cls, text):
        tokens = re.findall(r'"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+', re.sub(r'//[^\n]*', '', text))
        index = 0
        def token():
            nonlocal index
            value = tokens[index]
            index += 1
            return value[1:-1] if value.startswith('"') else value
        def body(end=False):
            items = []
            while index < len(tokens):
                key = token()
                if key == '}':
                    require(end, "unexpected KV closing brace")
                    return cls(items)
                if index >= len(tokens):
                    break  # a dangling key at EOF (seen in some Steam .acf files) carries no value
                value = token()
                items.append((key, body(True) if value == '{' else value))
            require(not end, "unterminated KV block")
            return cls(items)
        return body()

    def get(self, key, default=None):
        return next((v for k, v in reversed(self.items) if k.lower() == key.lower()), default)

    def set(self, key, value):
        self.items = [(k, v) for k, v in self.items if k.lower() != key.lower()]
        self.items.append((key, str(value)))

    def blocks(self, name=None):
        for key, value in self.items:
            if isinstance(value, KV):
                if name is None or key == name:
                    yield value
                yield from value.blocks(name)

    def text(self, depth=0):
        indent = '\t' * depth
        out = []
        for key, value in self.items:
            if isinstance(value, KV):
                out.append(f'{indent}{key}\n{indent}{{\n{value.text(depth + 1)}{indent}}}\n')
            else:
                out.append(f'{indent}"{key}" "{value}"\n')
        return ''.join(out)


def vtf_rgba(path, pixels, width=8, height=8, normal=False):
    """VTF 7.2: 80-byte header, no thumbnail, one uncompressed RGBA8888 mip."""
    require(len(pixels) == width * height * 4, "VTF pixels")
    header = bytearray(80)
    struct.pack_into('<4sIIIHHIHH', header, 0, b'VTF\0', 7, 2, 80, width, height,
                     0x2000 | 0x200 | 0x100 | 0x10 | 0x20 | (0x80 if normal else 0), 1, 0)
    struct.pack_into('<3f', header, 32, 0.5, 0.5, 0.5)
    struct.pack_into('<fIBI', header, 48, 1.0, 0, 1, 0xffffffff)
    struct.pack_into('<BBH', header, 61, 0, 0, 1)
    Path(path).write_bytes(header + pixels)


class VPK:
    def __init__(self, path):
        self.path = Path(path)
        with self.path.open('rb') as file:
            sig, version, size = struct.unpack('<3I', file.read(12))
            require(sig == 0x55aa1234 and version in (1, 2), f"invalid VPK {path}")
            self.header = 12 if version == 1 else 28
            file.seek(self.header)
            tree = file.read(size)
        self.tree_size = size
        self.entries = {}
        at = 0
        def string():
            nonlocal at
            end = tree.index(0, at)
            value = tree[at:end].decode('utf-8')
            at = end + 1
            return value
        while True:
            extension = string()
            if not extension:
                break
            while True:
                folder = string()
                if not folder:
                    break
                while True:
                    name = string()
                    if not name:
                        break
                    checksum, preload, archive, offset, length, terminal = unpack('<IHHIIH', tree, at)
                    at += 18
                    require(terminal == 0xffff, "VPK terminator")
                    prefix = tree[at:at + preload]
                    at += preload
                    full = (('' if folder == ' ' else folder + '/') + name + '.' + extension).lower()
                    self.entries[full] = (checksum, archive, offset, length, prefix)

    def read(self, name):
        entry = self.entries.get(name.lower())
        if entry is None:
            return None
        checksum, archive, offset, length, prefix = entry
        if archive == 0x7fff:
            path, offset = self.path, self.header + self.tree_size + offset
        else:
            path = self.path.with_name(self.path.name.replace('_dir.vpk', f'_{archive:03d}.vpk'))
        with path.open('rb') as file:
            file.seek(offset)
            raw = prefix + file.read(length)
        require(crc(raw) == checksum, f"VPK CRC {name}")
        return raw


def discover_content():
    steam = []
    if os.name == 'nt':
        import winreg
        for hive, key, field in ((winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath'),
                                 (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath')):
            try:
                with winreg.OpenKey(hive, key) as handle:
                    steam.append(Path(winreg.QueryValueEx(handle, field)[0]))
            except OSError:
                pass
    steam += [Path(os.environ.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Steam', Path('E:/SteamLibrary')]
    libraries = list(steam)
    for root in steam:
        path = root / 'steamapps/libraryfolders.vdf'
        if path.is_file():
            tree = KV.parse(path.read_text(encoding='utf-8-sig'))
            for block in tree.blocks():
                value = block.get('path')
                if value:
                    libraries.append(Path(value.replace('\\\\', '\\')))
    apps = {}
    for root in dict.fromkeys(libraries):
        folder = root / 'steamapps'
        if not folder.is_dir():
            continue
        for manifest in folder.glob('appmanifest_*.acf'):
            tree = KV.parse(manifest.read_text(encoding='utf-8-sig'))
            for block in tree.blocks():
                appid, installed = block.get('appid'), block.get('installdir')
                if appid and installed:
                    apps[appid] = folder / 'common' / installed
    missing = [app for app in ('243750', '243730', '220') if app not in apps or not apps[app].is_dir()]
    require(not missing, 'Missing installed Steam content/appmanifest: ' + ', '.join(missing) + '; install SDK Base 2013 Multiplayer, Singleplayer, and Half-Life 2 with episode content')
    if '290930' not in apps:
        apps['290930'] = apps['220']  # Current HL2 owns the formerly separate Lost Coast content mount.
    return apps


def model_declares_texel_lighting(mdl, load):
    """Inspect actual studio keyvalues and referenced materials, including patches."""
    require(len(mdl)>=320 and mdl[:4]==b'IDST', "studio header")
    def declared(raw):
        tree = KV.parse(raw.decode('utf-8',errors='replace').rstrip('\0'))
        for block in (tree, *tree.blocks()):
            value = block.get('$lightmapres')
            if not isinstance(value,str):
                continue
            try:
                sizes = [float(v) for v in value.strip(' []()').split()]
            except ValueError:
                continue
            if sizes and all(math.isfinite(v) and v>0 for v in sizes):
                return True
        return False
    kv_offset,kv_size = unpack('<2i',mdl,312)
    require(kv_offset>=0 and kv_size>=0 and kv_offset+kv_size<=len(mdl), "studio keyvalues")
    if kv_size and declared(mdl[kv_offset:kv_offset+kv_size]):
        return True
    textures,texture_at,paths,path_at = unpack('<4i',mdl,204)
    require(textures>=0 and texture_at>=0 and texture_at+textures*64<=len(mdl), "studio textures")
    require(paths>=0 and path_at>=0 and path_at+paths*4<=len(mdl), "studio material paths")
    def text(at):
        require(0<=at<len(mdl), "studio string")
        end = mdl.find(b'\0',at)
        require(end>=at, "unterminated studio string")
        return mdl[at:end].decode('ascii').replace('\\','/')
    directories = [text(unpack('<i',mdl,path_at+i*4)[0]) for i in range(paths)] + ['']
    pending = []
    for index in range(textures):
        at = texture_at+index*64
        name = text(at+unpack('<i',mdl,at)[0])
        pending.extend('materials/'+directory.rstrip('/')+'/'+name+'.vmt' if directory
                       else 'materials/'+name+'.vmt' for directory in directories)
    visited = set()
    while pending:
        name = pending.pop().lower()
        if name in visited:
            continue
        visited.add(name)
        raw = load(name)
        if raw is None:
            continue
        if declared(raw):
            return True
        tree = KV.parse(raw.decode('utf-8',errors='replace'))
        for block in (tree, *tree.blocks()):
            include = block.get('include')
            if isinstance(include,str):
                include = include.replace('\\','/').lower()
                pending.append(('' if include.startswith('materials/') else 'materials/')
                               + include + ('' if include.endswith('.vmt') else '.vmt'))
    return False


def prepare(fixture, toolgame):
    require(toolgame.resolve() != (GAME / 'mod_episodic').resolve(), 'refusing to rewrite runtime gameinfo')
    fixture.mkdir(parents=True, exist_ok=True)
    toolgame.mkdir(parents=True, exist_ok=True)
    apps = discover_content()
    original = (GAME / 'mod_episodic/gameinfo.txt').read_text(encoding='utf-8-sig')
    tree = KV.parse(original)
    for block in tree.blocks('SearchPaths'):
        for index, (key, value) in enumerate(block.items):
            if not isinstance(value, str):
                continue
            match = re.match(r'\|appid_(\d+)\|(.*)', value)
            if match:
                require(match[1] in apps, f'undiscovered appid {match[1]}')
                mounted = apps[match[1]] / match[2]
                if mounted.suffix == '.vpk' and not mounted.exists():
                    mounted = mounted.with_name(mounted.stem + '_dir.vpk')
                require(mounted.exists(), f'missing mounted content {mounted}')
                block.items[index] = (key, str(mounted).replace('\\', '/'))
            elif value == 'mod_episodic/custom/*':
                block.items[index] = (key, '|gameinfo_path|custom/*')
    (toolgame / 'gameinfo.txt').write_text(tree.text(), encoding='utf-8')
    roots = [apps['243730'] / 'ep2', apps['243730'] / 'episodic', apps['243750'] / 'hl2', apps['243730'] / 'hl2', apps['220'] / 'ep2']
    vpks = [VPK(path) for root in roots for path in root.glob('*_dir.vpk')]
    integration = GAME / 'mod_episodic/maps/ep2_outland_09.bsp'
    require(integration.is_file(), f'missing existing static-prop dictionary fixture {integration}')
    names, _ = BSP(integration).static_dictionary()
    model = None
    def load(name):
        for root in roots:
            loose = root / name
            if loose.is_file():
                return loose.read_bytes()
        for archive in vpks:
            data = archive.read(name)
            if data is not None:
                return data
        return None
    texel_lighting = False
    for name in sorted(names, key=lambda n: ('crate' not in n.lower(), 'props_junk' not in n.lower(), n)):
        base = name[:-4]
        mdl = load(name)
        if mdl and mdl[:4] == b'IDST' and load(base + '.vvd') and load(base + '.dx90.vtx'):
            bounds = unpack('<6f', mdl, 104)
            if any(bounds[k+3]-bounds[k] > 96 for k in range(3)):
                continue
            if model is None:
                model = name
            if model_declares_texel_lighting(mdl,load):
                model,texel_lighting = name,True
                break
    require(model, 'no loadable MDL/VVD/DX90.VTX from ep2_outland_09 static-prop dictionary in mounted episode content')
    material = toolgame / 'materials/shadowmap_test'
    material.mkdir(parents=True, exist_ok=True)
    # 186/255 in gamma space is neutral approximately 0.5 linear albedo.
    vtf_rgba(material / 'neutral.vtf', bytes((186, 186, 186, 255)) * 64)
    vtf_rgba(material / 'normal.vtf', bytes((128, 128, 255, 255)) * 64, normal=True)
    vtf_rgba(material / 'cutout.vtf', b''.join(bytes((186, 186, 186, 255 if (x + y) % 2 else 0)) for y in range(8) for x in range(8)))
    for name, shader, extra in (('flat', 'LightmappedGeneric', ''), ('bump', 'LightmappedGeneric', '"$bumpmap" "shadowmap_test/normal"'),
                                 ('cutout', 'LightmappedGeneric', '"$alphatest" "1"\n"$nocull" "1"')):
        texture = 'cutout' if name == 'cutout' else 'neutral'
        (material / (name + '.vmt')).write_text(f'"{shader}"\n{{\n"$basetexture" "shadowmap_test/{texture}"\n"$reflectivity" "[0.5 0.5 0.5]"\n{extra}\n}}\n')
    (material / 'detail.vmt').write_text('"UnlitGeneric"\n{\n"$basetexture" "shadowmap_test/cutout"\n"$vertexcolor" "1"\n"$vertexalpha" "1"\n"$alphatest" "1"\n"$nocull" "1"\n}\n')
    (toolgame / 'maps').mkdir(exist_ok=True)
    (toolgame / 'maps/shadowmap_detail.vbsp').write_text('"detail"\n{\n}\n')
    base = KV.parse(Path(__file__).with_name('shadowmap_fixture.vmf').read_text())
    for entity in base.blocks('entity'):
        if entity.get('classname') == 'prop_static':
            entity.set('model', model)
            entity.set('generatelightmaps',int(texel_lighting))
    def variant(name, keep=None, mutate=None):
        require('.' not in name, 'VBSP variant names must not contain dots')
        vmf = KV.parse(base.text())
        if keep is not None:
            vmf.items = [(key, value) for key, value in vmf.items if key != 'entity' or value.get('classname') not in ('light', 'light_spot', 'light_environment') or keep(value)]
        if mutate:
            mutate(vmf)
        (fixture / (name + '.vmf')).write_text(vmf.text(), encoding='utf-8')
    def set_lights(vmf, key, value, cls=None):
        for entity in vmf.blocks('entity'):
            if entity.get('classname') in ('light', 'light_spot', 'light_environment') and (cls is None or entity.get('classname') == cls):
                entity.set(key, value)
    sun = lambda e: e.get('classname') == 'light_environment'
    spot = lambda e: e.get('classname') == 'light_spot' and e.get('origin') == '1088 208 128'
    default = lambda e: e.get('classname') == 'light' and e.get('_shadowmap') != '1'
    opted = lambda e: e.get('classname') == 'light' and e.get('_shadowmap') == '1'
    variant('shadowmap_fixture')
    variant('point-only', default)
    variant('opted-point-only', opted)
    variant('sun-only', sun)
    variant('spot-only', spot)
    variant('wide-spot-only', lambda e: e.get('classname') == 'light_spot' and not spot(e))
    for name, value in (('sun-spread-0p27', '0.27'), ('sun-spread-0', '0'), ('sun-spread-invalid', 'nan'), ('sun-spread-invalid-infinity', 'inf'), ('sun-spread-invalid-negative', '-0.1'), ('sun-spread-invalid-upper', '90'), ('sun-zero-intensity', '0')):
        variant(name, sun, lambda v, key=('_light' if name == 'sun-zero-intensity' else 'SunSpreadAngle'), value=value: set_lights(v, key, '255 255 255 0' if key == '_light' else value))
    def sun_probe(vmf, radius=8, zero=False):
        # The exact floor grid point x=-256 bisects this vertical-sun cone.
        set_lights(vmf,'pitch','-90','light_environment')
        set_lights(vmf,'angles','0 0 0','light_environment')
        set_lights(vmf,'SunSpreadAngle',str(radius),'light_environment')
        if zero:
            set_lights(vmf,'_light','255 255 255 0','light_environment')
            set_lights(vmf,'_lightHDR','255 255 255 0','light_environment')
        corners = (
            ((-256,-256,128),(-384,-256,128),(-384,-384,128)),
            ((-256,-256,144),(-256,-384,144),(-384,-384,144)),
            ((-256,-384,144),(-256,-384,128),(-384,-384,128)),
            ((-384,-256,144),(-384,-256,128),(-256,-256,128)),
            ((-384,-384,144),(-384,-384,128),(-384,-256,128)),
            ((-256,-256,144),(-256,-256,128),(-256,-384,128)))
        solid = KV([('id','920000')])
        for index,points in enumerate(corners):
            side = KV([('id',str(920001+index)),
                       ('plane',' '.join('(%d %d %d)'%point for point in points)),
                       ('material','shadowmap_test/flat'),
                       ('uaxis','[1 0 0 0] 0.25' if index<4 else '[0 1 0 0] 0.25'),
                       ('vaxis','[0 -1 0 0] 0.25' if index<2 else '[0 0 -1 0] 0.25'),
                       ('rotation','0'),('lightmapscale','8'),('smoothing_groups','0')])
            solid.items.append(('side',side))
        vmf.get('world').items.append(('solid',solid))
    variant('sun-cone-probe',sun,sun_probe)
    variant('sun-cone-probe-zero',sun,lambda v:sun_probe(v,zero=True))
    variant('sun-central-probe',sun,lambda v:sun_probe(v,radius=0))
    for name, value in (('shadow-radius-zero', '0'), ('shadow-radius-invalid', '-1'), ('shadow-radius-invalid-nan', 'nan'), ('shadow-radius-invalid-infinity', 'inf'), ('shadow-radius-invalid-junk', 'four'), ('shadow-radius-eight', '8')):
        variant(name, spot, lambda v, value=value: set_lights(v, '_shadow_radius', value))
    def switchable(vmf):
        set_lights(vmf, 'targetname', 'fixture_switchable')
        set_lights(vmf, 'style', '32')
    variant('switchable-style32', spot, switchable)
    variant('opt-out', lambda e: sun(e) or spot(e), lambda v: set_lights(v, '_shadowmap', '0'))
    def multi(vmf):
        first = next(vmf.blocks('entity'))
        first.set('SunSpreadAngle', '0.11')
        other = KV.parse(KV([('entity', first)]).text()).get('entity')
        other.set('id', '900001')
        other.set('SunSpreadAngle', '0.53')
        vmf.items.append(('entity', other))
    variant('multi-env-last-wins', sun, multi)
    def overflow(vmf):
        for i in range(9):
            for j in range(8):
                vmf.items.append(('entity', KV([('id', str(910000 + i * 8 + j)), ('classname', 'light_spot'), ('origin', f'{112*i-448} {112*j-392} 256'), ('angles', '90 0 0'), ('pitch', '-90'), ('_inner_cone', '30'), ('_cone', '45'), ('_light', '255 255 255 200'), ('_distance', '512'), ('style', '0')])))
    variant('overflow-72-spots', lambda e: False, overflow)
    marker = dict(schema=1, model=model, count=4097, kind='client-static-renderables',
                  networkEdicts=False, origin=[-448, -448, 8], grid=[17, 241], spacing=[2, 2, 0],
                  validationCommand='r_shadowmap_validate_casters 1',
                  requiredLog='ShadowMapCasterValidation: testedCount=%d reportedCount=%d exhaustiveCount=%d mismatchCount=%d exceeds4096=%d')
    (fixture / 'renderables-4097.json').write_text(json.dumps(marker, indent=2))
    config = dict(schema=1, sdkBin=str(apps['243750'] / 'bin/x64'), model=model,
                  texelLighting=texel_lighting,
                  fixtureDir=str(fixture.resolve()), toolGame=str(toolgame.resolve()))
    (fixture / 'fixture-preparation.json').write_text(json.dumps(config, indent=2))
    return config


class Runner:
    def __init__(self, args, config):
        self.args, self.config = args, config
        self.root = Path(args.fixture_dir).resolve()
        self.game = Path(args.tool_game).resolve()
        self.env = os.environ.copy()
        self.env['PATH'] = config['sdkBin'] + os.pathsep + str(GAME / 'bin/x64') + os.pathsep + self.env.get('PATH', '')
        self.tools = {name: GAME / 'bin/x64' / (name + '.exe') for name in ('vbsp', 'vvis', 'vrad_restir')}
        for name, path in self.tools.items():
            require(path.is_file(), f'missing built installed tool {name}: {path}')
        self.cache = {}
        self.report = None
        for path in args.runtime_report or []:
            report = json.loads(path.read_text())
            require(report.get('schema') == 1, f'runtime report schema: {path}')
            if self.report is None:
                self.report = {'schema': 1}
            for key, value in report.items():
                if isinstance(value, list):
                    self.report.setdefault(key, []).extend(value)
                elif isinstance(value, dict):
                    self.report.setdefault(key, {}).update(value)
                else:
                    self.report[key] = value

    def command(self, tool, options, path, expect=None):
        command = [str(self.tools[tool]), *map(str, options), '-game', str(self.game), str(path)]
        proc = subprocess.run(command, env=self.env, cwd=SRC, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        log = self.root / (Path(path).stem + f'.{tool}.{len(list(self.root.glob(Path(path).stem + "." + tool + ".*.log")))}.log')
        log.write_text('COMMAND: ' + subprocess.list2cmdline(command) + '\n' + proc.stdout, encoding='utf-8')
        if expect:
            require(proc.returncode != 0 and any(line.strip().startswith(expect) for line in proc.stdout.splitlines()), f'expected rejection prefix {expect!r}; return={proc.returncode}; log={log}')
        else:
            require(proc.returncode == 0, f'{tool} failed ({proc.returncode}); log={log}')
        return proc.stdout

    def compile(self, variant):
        key = ('compile', variant)
        if key not in self.cache:
            work = self.root / 'compiled'
            work.mkdir(exist_ok=True)
            vmf = work / (variant + '.vmf')
            shutil.copyfile(self.root / (variant + '.vmf'), vmf)
            self.command('vbsp', [], vmf)
            bsp = vmf.with_suffix('.bsp')
            self.command('vvis', [], bsp)
            self.cache[key] = bsp
        return self.cache[key]

    def options(self, mode='both', feature=True, bounces=4, compute=False, diagnostics=None, extra=()):
        result = ['-final', '-' + mode, '-StaticPropLighting', '-TextureShadows', '-restir_iterations', '128', '-restir_seed', '1', '-restir_denoiser', 'none', '-restir_maxbounces', str(bounces)]
        if feature:
            result.append('-restir_shadowmaps')
        if compute:
            result.append('-restir_force_compute_bvh')
        if diagnostics:
            result += ['-restir_shadowmap_diagnostics', str(diagnostics)]
        return result + list(extra)

    def bake(self, variant, feature=True, bounces=4, compute=False):
        key = (variant, feature, bounces, compute)
        if key not in self.cache:
            directory = self.root / 'bakes'
            directory.mkdir(exist_ok=True)
            name = variant + ('-feature' if feature else '-ordinary') + f'-b{bounces}' + ('-compute' if compute else '-hardware')
            bsp = directory / (name + '.bsp')
            shutil.copyfile(self.compile(variant), bsp)
            text = self.command('vrad_restir', self.options(feature=feature, bounces=bounces, compute=compute), bsp)
            parsed = BSP(bsp)
            parsed.validate()
            self.cache[key] = (parsed, text)
        return self.cache[key]

    def pair(self, variant, bounces=4):
        return self.bake(variant, False, bounces)[0], self.bake(variant, True, bounces)[0]

    def runtime(self):
        require(self.report is not None, 'missing --runtime-report: run shaderapidx12_smoke -case shadowmaps -report <path> first')
        if 'pcss' in self.report or 'receiverCases' in self.report:
            require(self.report.get('draws',0)>0 and self.report.get('comparisons',0)>0,'smoke report lacks real compared GPU draws')
            require(self.report.get('shaderResourceABI')==3,'live smoke admission did not use shader resource ABI 3')
        return self.report

    def diagnostics(self, variant, feature, bounces=4, mode=0):
        key = ('diagnostics',variant,feature,bounces,mode)
        if key not in self.cache:
            directory = self.root/'diagnostics'
            directory.mkdir(exist_ok=True)
            stem = f"{variant}-{'feature' if feature else 'ordinary'}-b{bounces}-{'both' if feature else ('hdr' if mode else 'ldr')}"
            base = directory/(stem+'.json')
            bsp = directory/(stem+'.bsp')
            shutil.copyfile(self.compile(variant),bsp)
            self.command('vrad_restir',self.options('hdr' if mode else 'ldr',feature,bounces,diagnostics=base),bsp)
            for output_mode in (range(2) if feature else (mode,)):
                path = Path(str(base)+('.hdr.json' if output_mode else '.ldr.json'))
                require(path.is_file(),f'missing mode-specific pre-denoise readback {path}')
                report = json.loads(path.read_text())
                require(report.get('mode')==('hdr' if output_mode else 'ldr'),'diagnostic mode ownership')
                for keyname in ('sourceRadiance','receiverRadiance','luxelValid','faces','luxelPositions'):
                    require(keyname in report,f'diagnostics missing {keyname}')
                require(report['sourceRadiance'] and len(report['sourceRadiance'])==len(report['receiverRadiance']),'empty/mismatched source readback')
                self.cache[('diagnostics',variant,feature,bounces,output_mode)] = report
        return self.cache[key]

    def reject(self, path, exact, feature=True):
        before = Path(path).read_bytes()
        self.command('vrad_restir', self.options(feature=feature), path, exact)
        require(Path(path).read_bytes() == before, 'rejected conversion replaced original BSP')

    def format(self):
        for variant in ('shadowmap_fixture', 'sun-only', 'spot-only', 'point-only', 'switchable-style32'):
            ordinary, feature = self.pair(variant)
            ordinary.validate()
            feature.validate()
            if variant != 'point-only':
                require(feature.shadow is not None, f'{variant}: missing rshd')
        feature = self.bake('shadowmap_fixture')[0]
        require(feature.detail()[1], 'fixture lacks detail sprites')
        count = feature.static_dictionary()[1]
        require(count>0, 'fixture lacks static receivers')
        for prop in range(count):
            for mode in range(2):
                name = f"sp_{'hdr_' if mode else ''}{prop}.vhv"
                require(name in feature.pak and decode_vhv(feature.pak[name]), f'fixture lacks VHV: {name}')
        for prop in feature.texel_props():
            require(decode_ppl(feature.lookup_ppl(prop)), 'fixture lacks admitted legacy PPL receiver')
        self.receiver_inputs()

    def worldlight_omission(self):
        for variant in ('shadowmap_fixture','sun-only','spot-only','opted-point-only','wide-spot-only','switchable-style32'):
            ordinary,feature = self.pair(variant,0)
            require(feature.shadow and feature.shadow['runtime']==3,'converted modes not both present')
            for mode in range(2):
                controls,written = ordinary.worldlights(mode),feature.worldlights(mode)
                selected = feature.shadow['modes'][mode]['lights']
                require(feature.flags&(0x40000<<mode),'missing runtime direct level flag')
                for record in selected:
                    full = record['light']
                    matches = [w for w in controls if same_light(full,w)]
                    require(len(matches)==1,'full selected record has no unique ordinary control')
                    expected = matches[0]
                    for field,value in full.items():
                        if field=='flags':
                            require((value^expected[field])&~1==0,'selected full flags mismatch')
                            continue  # Only ordinary ambient inclusion may be recomputed.
                        if isinstance(value,tuple):
                            require(all(abs(x-y)<=1e-6*max(1,abs(x),abs(y)) for x,y in zip(value,expected[field])),'selected full vector mismatch: '+field)
                        elif isinstance(value,float):
                            require(abs(value-expected[field])<=1e-6*max(1,abs(value),abs(expected[field])),'selected full scalar mismatch: '+field)
                        else:
                            require(value==expected[field],'selected full integer mismatch: '+field)
                    require(not any(same_light(full,w) for w in written),'selected light remained engine-visible')
                survivors = [w for w in controls if not any(same_light(w,r['light']) for r in selected)]
                require(len(survivors)==len(written) and all(any(same_light(w,s) for s in survivors) for w in written),'unselected worldlight lost or changed')

    def implied_both(self):
        for requested in ('ldr','hdr'):
            path = self.root/('implied-both-'+requested+'.bsp')
            shutil.copyfile(self.compile('sun-only'),path)
            self.command('vrad_restir',self.options(requested,True),path)
            bsp = BSP(path)
            bsp.validate()
            require(bsp.shadow and bsp.shadow['runtime']==3 and bsp.flags&0xc0000==0xc0000,'single-mode feature invocation did not imply both')
            require(bsp.lumps[8] and bsp.lumps[53] and bsp.lumps[7] and bsp.lumps[58],'implied-both output mode missing')

    def rescale_both(self):
        path = self.root/'rescale-both.bsp'
        shutil.copyfile(self.compile('sun-only'),path)
        self.command('vrad_restir',self.options('ldr',True,extra=('-restir_lightmapscale','0.5')),path)
        bsp = BSP(path)
        bsp.validate()
        require(bsp.shadow and bsp.shadow['runtime']==3,'rescale implied-both ownership missing')
        require(bsp.shadow['modes'][0]['lightCount']==bsp.shadow['modes'][1]['lightCount'],'rescale second pass lost selected records')
        unscaled = self.bake('sun-only')[0]
        for mode in range(2):
            current = bsp.shadow['modes'][mode]
            previous = unscaled.shadow['modes'][mode]
            require(current['sunVisibilityCount']!=previous['sunVisibilityCount'] and
                    current['receiverDataCRC32']!=previous['receiverDataCRC32'],
                    'rescale retained obsolete receiver topology/mask data')
        require(bsp.shadow['modes'][0]['sunVisibility']==bsp.shadow['modes'][1]['sunVisibility'],
                'rescale second mode lost matching geometric mask')
        density = re.search(rb'"_restir_lightmapscale"\s+"([^"]+)"',bsp.lumps[0])
        require(density is not None and abs(float(density[1])-.5)<1e-7,'rescale density guard not recorded')

    def receiver_inputs(self):
        cases = []
        for mode in range(2):
            for name, variant, position, tint, scale, style in (
                    ('neutral', 'sun-only', (256,192,0), (1,1,1), 1.0, 0),
                    ('tinted', 'sun-only', (256,192,0), (.25,.5,.75), 1.0, 0),
                    ('style32', 'switchable-style32', (1504,224,0), (1,1,1), .375, 32),
                    ('point', 'opted-point-only', (256,-384,0), (1,1,1), 1.0, 0),
                    ('wide-spot', 'wide-spot-only', (0,384,0), (1,1,1), 1.0, 0)):
                ordinary, feature = self.pair(variant, 0)
                color, quantization = ordinary.samples(mode, position, style)
                require(max(color)>0, f'{name}: black ordinary calibration input')
                record = feature.shadow['modes'][mode]['lights'][0]
                light = dict(record['light'])
                light.update({key:value for key,value in record.items() if key!='light'})
                cases.append(dict(name=name, mode='hdr' if mode else 'ldr', light=light,
                                  receiverPos=position, receiverNormal=(0,0,1), tint=tint,
                                  albedo=(1,1,1), styleScale=scale, ordinaryRGB=color,
                                  quantization=[quantization]*3))
        (self.root / 'receivers.json').write_text(json.dumps(cases, indent=2))
        self.cache['receiver-inputs'] = cases

    def selected_zero_bounce(self):
        for variant, positions in (('sun-only', ((256, 192, 0),)), ('spot-only', ((1504, 224, 0),)), ('opted-point-only', ((256, -384, 0),))):
            ordinary, feature = self.pair(variant, 0)
            for mode in range(2):
                for position in positions:
                    control, q = ordinary.samples(mode, position)
                    receiver, qr = feature.samples(mode, position)
                    require(max(control) > 0.01, f'{variant}: control has no selected direct')
                    require(max(receiver) == 0, f'{variant}: selected direct leaked into receiver')

    def indirect_corner(self):
        ordinary, feature = self.pair('spot-only', 4)
        for mode in range(2):
            a, qa = ordinary.samples(mode, (1440, 128, 0))
            b, qb = feature.samples(mode, (1440, 128, 0))
            require(max(a) > 0 and max(b) > 0, 'indirect corner lost bounced energy')
            close_rgb(a, b, qa, qb, 'indirect corner')

    def full_transport(self):
        for variant in ('sun-only', 'spot-only', 'point-only', 'opted-point-only'):
            for mode in range(2):
                a, b = self.diagnostics(variant, False, mode=mode), self.diagnostics(variant, True, mode=mode)
                require(a['sourceRadiance'] == b['sourceRadiance'], f'{variant}/{mode}: pre-denoise full transport changed')

    def default_point(self):
        ordinary, feature = self.pair('point-only', 0)
        for mode in range(2):
            require(ordinary.lumps[MODE_LUMPS[mode][0]] == feature.lumps[MODE_LUMPS[mode][0]], 'default point direct changed')
            require(ordinary.worldlights(mode)==feature.worldlights(mode),'default point ordinary worldlight changed')
        require(feature.shadow is None,'default point selected metadata')

    def opted_point(self):
        feature = self.bake('opted-point-only', bounces=0)[0]
        for mode in range(2):
            records = feature.shadow['modes'][mode]['lights']
            require(len(records)==1 and records[0]['light']['type']==1,'opted point must own one complete cube-light record')
        self.selected_zero_bounce()
        self.calibration_case('point')

    def wide_spot(self):
        feature = self.bake('wide-spot-only')[0]
        for mode in range(2):
            rec = feature.shadow['modes'][mode]['lights']
            require(len(rec) == 1, 'wide spot selected count')
            light = rec[0]['light']
            require(light['type'] == 2 and light['stopdot2'] <= math.cos(math.radians(89)) + 1e-5 and light['stopdot'] > light['stopdot2'], 'wide spot lost cone attenuation / cube threshold')
        self.calibration_case('wide-spot')

    def opt_out(self):
        ordinary, feature = self.pair('opt-out')
        for mode in range(2):
            require(ordinary.lumps[MODE_LUMPS[mode][0]] == feature.lumps[MODE_LUMPS[mode][0]], 'opt-out transport changed')
            require(ordinary.worldlights(mode)==feature.worldlights(mode),'opt-out light removed from worldlights')
        require(feature.shadow is None,'opt-out selected metadata')

    def sun_visibility(self, zero=False):
        variant = 'sun-cone-probe-zero' if zero else 'sun-cone-probe'
        feature = self.bake(variant,bounces=0)[0]
        control = self.bake('sun-cone-probe',bounces=0)[0]
        central = self.bake('sun-central-probe',bounces=0)[0]
        bounced = self.bake(variant,bounces=4)[0]
        allowed = {int(n*255/32+.5) for n in range(33)}
        for mode in range(2):
            rec = feature.shadow['modes'][mode]
            require(rec['sunLightIndex']==0 and rec['lights'][0]['light']['type']==3,'runtime sun missing')
            require(set(rec['sunVisibility'])<=allowed,'mask is not a quantized 32-ray fraction')
            require(set(central.shadow['modes'][mode]['sunVisibility'])<={0,255},
                    'zero-radius mask must use one central ray')
            require(rec['sunVisibility']==bounced.shadow['modes'][mode]['sunVisibility'],
                    'transport bounces changed independent visibility')
            if zero:
                require(all(v==0 for v in rec['lights'][0]['light']['intensity']),'authored zero sun intensity changed')
                require(rec['sunVisibility']==control.shadow['modes'][mode]['sunVisibility'],
                        'zero-intensity sun changed independent visibility')
            for position,expected in (((-320,-320,0),0),((-192,-320,0),255),
                                      ((1400,0,0),0),((192,64,1),255),((320,64,1),255)):
                value,_,_ = feature.sun_sample(mode,position)
                require(value==expected,f'{variant}/{mode}: traced visibility at {position}: {value} != {expected}')
            edge = feature.sun_sample(mode,(-256,-320,0))[0]
            require(0<edge<255,f'{variant}/{mode}: angular roof edge was not partially visible')
            # Actual black receiver RGB must not identify visibility: two
            # distinct floor faces carry the same black bytes and opposite masks.
            black = []
            face_ids = []
            for position in ((-192,-320,0),(1400,0,0)):
                value,index,_ = feature.sun_sample(mode,position)
                face = next(f for f in feature.faces(mode) if f['index']==index)
                at = face['offset']
                count = math.prod(face['size'])*face['channels']*len(face['styles'])*4
                data = feature.lumps[MODE_LUMPS[mode][0]][at:at+count]
                require(all(max(rgbexp(data[i:i+4]))==0 for i in range(0,len(data),4)),
                        'black-face control contains RGB radiance')
                black.append(feature.samples(mode,position)[0])
                face_ids.append(index)
            require(face_ids[0]!=face_ids[1] and black[0]==black[1], 'distinct identical-black face proof missing')
            require(any(f['flags']&1 for f in rec['receiverFaces']) and
                    any(f['flags']&2 for f in rec['receiverFaces']) and
                    any(f['flags']==3 for f in rec['receiverFaces']),
                    'scalar layout lacks bump/displacement/bumped-displacement coverage')
        require(feature.shadow['modes'][0]['sunVisibility']==feature.shadow['modes'][1]['sunVisibility'],
                'HDR/LDR changed geometric visibility')
        self.sun_carrier_runtime()
        pcss = self.runtime()['pcss']
        for key in ('noBlockersFullyLit','centerBlockerDetected','slopedNoAcne','clearNotAveraged'):
            require(pcss[key] is True,'runtime sun visibility proof failed: '+key)
        require(pcss['seamContamination'] is False,'runtime sun visibility contaminated')
        self.pcss_radii()
        self.pcss_contact()

    def sun_carrier_runtime(self):
        report = self.runtime().get('sunVisibility',{})
        formats = report.get('formats',[])
        expected = {'ldr-rgba8888','hdr-rgba16161616','hdr-rgba16161616f'}
        require({case['name'] for case in formats}==expected,'missing actual LDR/HDR companion readbacks')
        for case in formats:
            label = case['name']
            values = [case[key] for key in ('blocked','open','fractional','filteredLeft','filteredRight')]
            require(all(math.isfinite(value) for value in values),label+': nonfinite carrier GPU pixels')
            require(0<=case['blocked']<.001 and case['open']>.01,label+': identical-black receiver masks leaked')
            require(abs(case['fractional']/case['open']-128/255)<=.01,label+': fractional sun diffuse/specular cap')
            require(abs(case['filteredLeft']-.25)<=.01 and abs(case['filteredRight']-.75)<=.01,
                    label+': companion must use linear filtering')
            require(case['beforeMappedFaces']==0 and case['firstDrawMappedFaces']>0 and
                    case['mappedFaces']>=case['firstDrawMappedFaces'] and case['mappedFaces']>=2 and
                    case['pages']>0 and case['unresolvedDraws']==0,label+': first-draw exact mapping missing')
            require(case['reuploadStable'] is True and case['alphaPreserved'] is True,
                    label+': RGB whole-page upload erased companion or changed alpha')
        bumped = report.get('bumped',[])
        require({case['name'] for case in bumped}=={'normal','ssbump'},'missing bumped/SSBump carrier GPU proof')
        for case in bumped+[report.get('displacement',{}),report.get('regularNonplanar',{})]:
            require(math.isfinite(case.get('blocked',float('nan'))) and
                    math.isfinite(case.get('open',float('nan'))) and
                    abs(case['blocked'])<=.01 and abs(case['open']-1)<=.01,
                    'bump/displacement/nonplanar scalar coordinate proof failed')
        for key in ('displacement','regularNonplanar'):
            case = report.get(key,{})
            require(case.get('mappedFaces')==2 and case.get('unresolvedDraws')==0,
                    key+': actual reference geometry mapping failed')
        decals = report.get('brushDecals',{})
        for key,expected in (('world',0),('brush',1),('worldDecal',0),('brushDecal',1),('genericDecal',0),
                             ('batchedWorld',0),('batchedBrush',1)):
            value = decals.get(key,float('nan'))
            require(math.isfinite(value) and abs(value-expected)<=.01,'world/runtime-only decal field: '+key)
        require(decals.get('unresolvedDraws')==0,'proven decal footprint rejected')
        cells = report.get('allocationCells',{})
        require(cells.get('mappedFaces')==2 and cells.get('pages')==1 and
                cells.get('unresolvedDraws')==0 and cells.get('rgbAlphaPreserved') is True,
                'allocation-clamped decals changed RGB/alpha or failed mapping')
        offsets = {(x,y) for x in (-.49,0,.49) for y in (-.49,0,.49) if x or y}
        cases = cells.get('cases',[])
        require({(case['receiver'],tuple(round(v,2) for v in case['offsetTexels'])) for case in cases}==
                {(receiver,offset) for receiver in ('generic-decal','decal') for offset in offsets},
                'missing allocation edge/corner receiver coverage')
        for case in cases:
            require(all(math.isfinite(case[key]) for key in ('blockedMax','openMin','rgbMaxError','alphaMaxError')) and
                    case['blockedMax']<.001 and case['openMin']>.999 and case['rgbMaxError']<.001 and
                    case['alphaMaxError']<.002 and case['outsideSampleCenterPixels']>0,
                    'allocation boundary blended a foreign/unproven field or changed alpha')
        rejections = report.get('allocationCellRejections',[])
        require({case['name'] for case in rejections}=={'before-base','unproven-containing-cell'},
                'missing unknown allocation-cell rejection proof')
        for case in rejections:
            require(case['statusFailed'] is True and case['unresolvedDraws']>0 and
                    case['error']=='Shadowmaps: unresolved baked sun receiver' and
                    math.isfinite(case['pixel']) and abs(case['pixel'])<.001,
                    'unknown decal allocation did not reject')
        displaced = report.get('displacementDecals',{})
        require(displaced.get('mappedFaces')==2 and displaced.get('pages')==1 and
                displaced.get('unresolvedDraws')==0 and displaced.get('rgbAlphaPreserved') is True,
                'displacement decal carrier did not preserve mapping and original lightmap data')
        before,after = displaced.get('originalBefore',[]),displaced.get('originalAfter',[])
        require(len(before)==4 and before==after and
                abs(displaced.get('gridAllocationAlpha',before[3])-before[3])>64,
                'displacement decal did not distinguish original alpha UV from sun-only UV')
        paths = displaced.get('paths',[])
        require({case['receiver'] for case in paths}=={'generic-decal','decal'},
                'missing generic/specialized displacement decal GPU coverage')
        for case in paths:
            require(case['firstVertexIndex']>0 and case['cachedAlternations']>=2 and
                    case['correctedPixels']>1000 and case['ordinaryPixels']>1000 and
                    case['blockedPixels']>0 and case['openPixels']>0,
                    'displacement decal did not exercise indexed mixed/cached masked and open regions')
            require(all(math.isfinite(case[key]) for key in
                        ('blockedMax','openMin','ordinaryMin','scalarMaxError','alphaMaxError')) and
                    case['blockedMax']<.001 and case['openMin']>.999 and case['ordinaryMin']>.999 and
                    case['scalarMaxError']<.003 and case['alphaMaxError']<.002,
                    'displacement decal sun coordinates or untouched alpha sampling are incorrect')
        thin = displaced.get('largeWorldThinEdge',{})
        require(thin.get('areaRuleUncoveredFraction',0)>thin.get('oldAreaThreshold',float('inf')) and
                thin.get('checkedPixels',0)>1000 and thin.get('mappedFaces')==2 and
                thin.get('unresolvedDraws')==0 and thin.get('rgbAlphaPreserved') is True,
                'missing large-coordinate float32 clip-edge GPU proof')
        require(all(math.isfinite(thin.get(key,float('nan'))) for key in
                    ('scalarMin','scalarMax','scalarMaxError','alphaMaxError')) and
                thin['scalarMin']<.15 and thin['scalarMax']>.85 and
                thin['scalarMaxError']<.003 and thin['alphaMaxError']<.002,
                'large-coordinate thin decal scalar/alpha mismatch')
        crossed = displaced.get('crossTriangle',{})
        require(crossed.get('sameDface') is True and crossed.get('noWholeTriangleSupport') is True and
                len(set(crossed.get('supportTriangles',[])))>1 and
                crossed.get('checkedPixels',0)>1000 and crossed.get('mappedFaces')==2 and
                crossed.get('pages')==1 and crossed.get('unresolvedDraws')==0 and
                crossed.get('rgbAlphaPreserved') is True,
                'missing same-face noncoplanar displacement decal GPU proof')
        require(crossed.get('blockedPixels',0)>0 and crossed.get('openPixels',0)>0 and
                all(math.isfinite(crossed.get(key,float('nan'))) for key in
                    ('blockedMax','openMin','scalarMaxError','alphaMaxError')) and
                crossed['blockedMax']<.001 and crossed['openMin']>.999 and
                crossed['scalarMaxError']<.003 and crossed['alphaMaxError']<.002,
                'cross-triangle decal scalar/alpha mismatch')
        unsigned = displaced.get('unsignedArea',{})
        sums = unsigned.get('weightSums',[])
        require(len(sums)==3 and all(math.isfinite(value) and abs(1-value)<.001 for value in sums) and
                sum(value>1+8*2**-23 for value in sums)>=2 and
                unsigned.get('minimumLiftedPlaneDistance',0)>1 and unsigned.get('outsideFlatCorners',0)>=2 and
                unsigned.get('sunUVFromOriginalQuery') is True and unsigned.get('checkedPixels',0)>1000 and
                unsigned.get('mappedFaces')==2 and unsigned.get('unresolvedDraws')==0 and
                unsigned.get('rgbAlphaPreserved') is True,
                'missing Source unsigned-area world-position drift GPU proof')
        require(unsigned.get('blockedPixels',0)>0 and unsigned.get('openPixels',0)>0 and
                all(math.isfinite(unsigned.get(key,float('nan'))) for key in
                    ('blockedMax','openMin','scalarMaxError','alphaMaxError')) and
                unsigned['blockedMax']<.001 and unsigned['openMin']>.999 and
                unsigned['scalarMaxError']<.003 and unsigned['alphaMaxError']<.002,
                'unsigned-area reconstruction changed sun coordinates or original alpha')
        perturbation = unsigned.get('rejectedPerturbation',{})
        require(perturbation.get('normalDistance',0)>perturbation.get('representationBound',float('inf')) and
                perturbation.get('planarUVRecomputed') is True and perturbation.get('rawUVInForeignOpen') is True and
                perturbation.get('knownBaseNormals') is True and perturbation.get('knownQuad') is True and
                perturbation.get('statusFailed') is True and perturbation.get('mappedFaces')==2 and
                perturbation.get('unresolvedDraws',0)>0 and
                perturbation.get('error')=='Shadowmaps: unresolved baked sun receiver' and
                perturbation.get('clearedFramebufferUnchanged') is True and
                math.isfinite(perturbation.get('framebufferMax',float('nan'))) and
                abs(perturbation['framebufferMax'])<.001 and
                math.isfinite(perturbation.get('clearAlphaMaxError',float('nan'))) and
                perturbation['clearAlphaMaxError']<.002,
                'unsigned-area reconstruction admitted geometry outside the actual Source output surface')
        grid_edge = displaced.get('quantizedGridEdge',{})
        require(grid_edge.get('gridSelectedUpper') is True and
                grid_edge.get('signedFlatWeight',0)<0 and
                1<grid_edge.get('weightSum',0)<1.001 and
                grid_edge.get('exactFloat32Forward') is True and
                grid_edge.get('exactObservedNormalLifts') is True and
                grid_edge.get('planeDistance',0)>grid_edge.get('planeRepresentationBound',float('inf')) and
                grid_edge.get('rawUVInForeignOpen') is True and grid_edge.get('checkedPixels',0)>1000 and
                grid_edge.get('statusReady') is True and grid_edge.get('mappedFaces')==2 and
                grid_edge.get('pages')==1 and grid_edge.get('unresolvedDraws')==0 and
                grid_edge.get('rgbAlphaPreserved') is True,
                'missing independently rounded Source grid/flat-coordinate edge proof')
        require(all(math.isfinite(grid_edge.get(key,float('nan'))) for key in
                    ('scalarMin','scalarMax','scalarMaxError','alphaMaxError','planeDistance','planeRepresentationBound')) and
                grid_edge['scalarMax']>grid_edge['scalarMin']+.04 and
                grid_edge['scalarMaxError']<.003 and grid_edge['alphaMaxError']<.002,
                'quantized grid-edge reconstruction changed sun coordinates or original alpha')
        rejected = displaced.get('rejections',[])
        require({case['name'] for case in rejected}==
                {'unsupported-lift','unknown-base-normal','unplaced-displacement'},
                'missing unresolved displacement decal rejection proofs')
        for case in rejected:
            require(case['statusFailed'] is True and case['unresolvedDraws']>0 and
                    case['error']=='Shadowmaps: unresolved baked sun receiver' and
                    case['rawUVInForeignOpen'] is True and
                    case['ownBasePlaced']==(case['name']!='unplaced-displacement') and
                    math.isfinite(case['framebufferMax']) and abs(case['framebufferMax'])<.001,
                    'unresolved displacement decal accepted a foreign allocation or geometric fallback')
        rollover = report.get('samplerRollover',{})
        for key,pixels in (('receiver',{'blocked':0,'open':1,'ordinary':1,'restored':0}),
                           ('shadowPass',{'before':0,'control':1,'after':0})):
            case = rollover.get(key,{})
            require(case.get('fenceAfter')==case.get('fenceBefore',-2)+1 and
                    case.get('statusReady') is True and case.get('alphaPreserved') is True,
                    key+': sampler exhaustion did not preserve the active recording scope')
            for pixel,expected in pixels.items():
                value = case.get(pixel,float('nan'))
                require(math.isfinite(value) and abs(value-expected)<.001,
                        key+': sampler rollover changed '+pixel)
        nested = report.get('nested',{})
        for key,expected in (('blockedBefore',0),('ordinary',1),('blockedAfter',0),
                             ('openChild',1),('blockedRestored',0)):
            value = nested.get(key,float('nan'))
            require(math.isfinite(value) and abs(value-expected)<=.01,'nested receiver restoration: '+key)
        ambiguous = report.get('ambiguous',[])
        require({case['name'] for case in ambiguous}==
                {'coincident-differing-mask','overlap-differing-geometry-mask'},
                'missing real ambiguous geometry rejection proofs')
        for case in ambiguous:
            require(case['statusFailed'] is True and
                    case['error']=='Shadowmaps: unresolved baked sun receiver' and
                    case['unresolvedDraws']>0 and math.isfinite(case['pixel']) and abs(case['pixel'])<=.001,
                    case['name']+': unresolved differing masks did not fail closed')

    def receiver_layout(self):
        for variant in ('sun-only','spot-only','switchable-style32'):
            for bsp in self.pair(variant,0):
                for mode in range(2):
                    lighting = bsp.lumps[MODE_LUMPS[mode][0]]
                    require(len(lighting)%4 == 0,'RGBExp32 lighting stride')
                    ranges = []
                    bumped = displacement = False
                    for face in bsp.faces(mode):
                        if face['offset'] < 0:
                            require(not face['styles'],'unlit face carries styles')
                            continue
                        require(face['styles'] and face['styles'][0]==0,'missing base style')
                        count = math.prod(face['size'])*face['channels']*len(face['styles'])
                        start = face['offset']-4*len(face['styles'])
                        end = face['offset']+4*count
                        require(start>=0 and end<=len(lighting) and start%4==0,'style/bump/average-prefix range')
                        ranges.append((start,end))
                        bumped |= face['channels']==4
                        displacement |= face['disp']>=0
                    for first,second in zip(sorted(ranges),sorted(ranges)[1:]):
                        require(first[1]<=second[0],'face receiver planes overlap')
                    require(bumped and displacement,'fixture lacks bump/displacement receiver planes')

    def receiver_gathers(self):
        for variant in ('sun-only','spot-only','point-only'):
            ordinary,feature = self.pair(variant,0)
            for mode in range(2):
                vertices_a,vertices_b = [],[]
                for prop in range(feature.static_dictionary()[1]):
                    name = f"sp_{'hdr_' if mode else ''}{prop}.vhv"
                    require(name in ordinary.pak and name in feature.pak,f'missing static receiver {name}')
                    a,b = decode_vhv(ordinary.pak[name]),decode_vhv(feature.pak[name])
                    require(len(a)==len(b) and a,'VHV vertices')
                    vertices_a.extend(a)
                    vertices_b.extend(b)
                if variant=='point-only':
                    require(vertices_a==vertices_b,'ordinary point VHV changed')
                else:
                    require_direct_removed(vertices_a,vertices_b,'VHV',gamma=True)
                for ambient in MODE_LUMPS[mode][3:]:
                    require(ordinary.lumps[ambient]==feature.lumps[ambient],
                            'full-source ambient gather changed')
            # Standard dprp/PPL are shared legacy payloads, not per-mode state.
            # -both's final HDR pass supplies these consumer-visible records.
            details = feature.detail()[1]
            if variant=='point-only':
                require(ordinary.detail()[1]==details and ordinary.game('dplh')==feature.game('dplh'),'default point detail changed')
            else:
                require_direct_removed([r[28:32] for r in ordinary.detail()[1]],
                                       [r[28:32] for r in details],'shared detail base')
            props = feature.texel_props()
            require(props==ordinary.texel_props(),'PPL admission changed')
            texels_a,texels_b = [],[]
            for prop in props:
                a,b = decode_ppl(ordinary.lookup_ppl(prop)),decode_ppl(feature.lookup_ppl(prop))
                require(len(a)==len(b) and a,'PPL texels')
                texels_a.extend(a)
                texels_b.extend(b)
            if props:
                if variant=='point-only':
                    require(texels_a==texels_b,'default point PPL changed')
                else:
                    require_direct_removed(texels_a,texels_b,'shared PPL',gamma=True)
            ordinary,feature = self.pair(variant,4)
            if variant!='point-only':
                require(any(max(rgbexp(r[28:32]))>0 for r in feature.detail()[1]),'detail bounce lost')
                require(any(any(max(v)>0 for v in decode_vhv(raw)) for name,raw in feature.pak.items() if name.endswith('.vhv')),'static bounce lost')
                if feature.texel_props():
                    require(any(any(max(v)>0 for v in decode_ppl(feature.lookup_ppl(p))) for p in feature.texel_props()),'texel bounce lost')
                require(all(any(feature.lumps[MODE_LUMPS[m][4]]) for m in range(2)),'ambient bounce lost')
            if variant=='spot-only':
                for mode in range(2):
                    compare_gamma_samples(decode_vhv(ordinary.pak[f"sp_{'hdr_' if mode else ''}1.vhv"]),decode_vhv(feature.pak[f"sp_{'hdr_' if mode else ''}1.vhv"]),'indirect static VHV')
                    ca,cb = ordinary.ambient_at(mode,(1440,128,8)),feature.ambient_at(mode,(1440,128,8))
                    require(any(max(rgbexp(v))>0 for v in cb),'indirect ambient corner black')
                    for va,vb in zip(ca,cb):
                        close_rgb(rgbexp(va),rgbexp(vb),quantum(va),quantum(vb),'indirect ambient cube')
                if 1 in feature.texel_props():
                    compare_gamma_samples(decode_ppl(ordinary.lookup_ppl(1)),decode_ppl(feature.lookup_ppl(1)),'indirect shared static PPL')
                a = next((r for r in ordinary.detail()[1] if unpack('<3f',r)==(1440,128,8)),None)
                b = next((r for r in feature.detail()[1] if unpack('<3f',r)==(1440,128,8)),None)
                require(a is not None and b is not None,'missing indirect corner detail receiver')
                va,vb = a[28:32],b[28:32]
                require(max(rgbexp(va))>0 and max(rgbexp(vb))>0,'indirect detail receiver black')
                close_rgb(rgbexp(va),rgbexp(vb),quantum(va),quantum(vb),'indirect shared detail')

    def backend_equivalence(self):
        for variant in ('sun-only', 'sun-cone-probe', 'sun-cone-probe-zero', 'sun-central-probe', 'spot-only', 'point-only'):
            a, text = self.bake(variant)
            require('hardware-rt' in text, 'hardware-RT backend unavailable; cannot claim backend comparison')
            b, text = self.bake(variant, compute=True)
            require('compute-bvh' in text, 'forced compute backend not selected')
            for mode in range(2):
                if a.shadow and a.shadow['modes'][mode]['sunLightIndex']>=0:
                    ra,rb = a.shadow['modes'][mode],b.shadow['modes'][mode]
                    require(ra['receiverFaces']==rb['receiverFaces'] and
                            ra['receiverTriangles']==rb['receiverTriangles'],'backend receiver geometry mismatch')
                    require(ra['sunVisibility']==rb['sunVisibility'],'hardware/compute traced masks differ')
                x, y = a.lumps[MODE_LUMPS[mode][0]], b.lumps[MODE_LUMPS[mode][0]]
                require(len(x) == len(y), 'backend topology mismatch')
                for i in range(0, len(x), 4):
                    close_rgb(rgbexp(x[i:i+4]), rgbexp(y[i:i+4]), quantum(x[i:i+4]), quantum(y[i:i+4]), f'backend luxel {i//4}')
                for name in a.pak:
                    if name.endswith('.vhv'):
                        require(name in b.pak,'backend VHV missing')
                        compare_gamma_samples(decode_vhv(a.pak[name]),decode_vhv(b.pak[name]),'backend VHV')
                ia,ib=a.lumps[MODE_LUMPS[mode][4]],b.lumps[MODE_LUMPS[mode][4]]
                require(len(ia)==len(ib),'backend ambient sample count')
                for at in range(0,len(ia),28):
                    require(ia[at+24:at+28]==ib[at+24:at+28],'backend ambient positions')
                    for direction in range(0,24,4):
                        va,vb=ia[at+direction:at+direction+4],ib[at+direction:at+direction+4]
                        close_rgb(rgbexp(va),rgbexp(vb),quantum(va),quantum(vb),'backend ambient')
            require(a.texel_props()==b.texel_props(),'backend PPL admission changed')
            for prop in a.texel_props():
                compare_gamma_samples(decode_ppl(a.lookup_ppl(prop)),decode_ppl(b.lookup_ppl(prop)),'backend shared PPL')
            da,db = a.detail()[1],b.detail()[1]
            require(len(da)==len(db),'backend detail count')
            for ra,rb in zip(da,db):
                va,vb = ra[28:32],rb[28:32]
                close_rgb(rgbexp(va),rgbexp(vb),quantum(va),quantum(vb),'backend shared detail')

    def ordinary_reconversion(self):
        converted = self.bake('shadowmap_fixture')[0]
        control = self.bake('shadowmap_fixture',False)[0]
        path = self.root/'reconvert.bsp'
        shutil.copyfile(converted.path,path)
        self.command('vrad_restir',self.options(feature=False),path)
        ordinary = BSP(path)
        ordinary.validate()
        require(ordinary.shadow is None,'ordinary -both reconversion retained rshd selected-light list')
        for mode in range(2):
            restored = ordinary.worldlights(mode)
            expected = control.worldlights(mode)
            require(len(restored)==len(expected) and all(any(same_light(w,e) for e in expected) for w in restored),'ordinary reconversion did not restore complete worldlights')
            for record in converted.shadow['modes'][mode]['lights']:
                require(any(same_light(record['light'],w) for w in restored),'ordinary reconversion lost previously selected light')

    def corrupt(self, scenario):
        good = self.bake('shadowmap_fixture')[0]
        sidecar = good.games['rshd']
        require(not sidecar['flags']&1,'fixture rshd unexpectedly compressed')
        base = sidecar['offset']
        cases = []
        for mode_index,mode in enumerate(good.shadow['modes']):
            header = base+16+mode_index*SHADOW_MODE_SIZE
            def word(key,value):
                return header+MODE_KEYS.index(key)*4,struct.pack('<I',value)
            def receiver_patch(offset,replacement):
                data = bytearray(sidecar['data'])
                data[offset:offset+len(replacement)] = replacement
                payload = b''.join(data[mode[key]:mode[key]+mode[count]*stride] for key,count,stride in (
                    ('receiverFacesOffset','receiverFaceCount',SHADOW_FACE_STRIDE),
                    ('receiverTrianglesOffset','receiverTriangleCount',SHADOW_TRIANGLE_STRIDE),
                    ('sunVisibilityOffset','sunVisibilityCount',1)))
                return [(base+offset,replacement),word('receiverDataCRC32',crc(payload))]
            if scenario=='malformed-counts':
                for key in ('lightCount','receiverFaceCount','receiverTriangleCount','sunVisibilityCount'):
                    cases.append((f'{mode_index}-{key}',[word(key,0xffffffff)]))
            elif scenario=='malformed-offsets':
                for key in ('lightsOffset','receiverFacesOffset','receiverTrianglesOffset','sunVisibilityOffset'):
                    for bad in (SHADOW_HEADER_SIZE+1,0xfffffffc):
                        cases.append((f'{mode_index}-{key}-{bad}',[word(key,bad)]))
            elif scenario=='malformed-crcs':
                for key in ('lightingCRC32','facesCRC32','worldlightsCRC32','receiverDataCRC32'):
                    cases.append((f'{mode_index}-{key}',[word(key,mode[key]^1)]))
                for key in ('receiverFacesOffset','receiverTrianglesOffset','sunVisibilityOffset'):
                    offset = mode[key]
                    cases.append((f'{mode_index}-{key}',[(base+offset,bytes((sidecar['data'][offset]^1,)))]))
            elif scenario=='malformed-overlap':
                keys = ('lightsOffset','receiverFacesOffset','receiverTrianglesOffset','sunVisibilityOffset')
                for destination in keys:
                    for source in keys:
                        if destination!=source:
                            cases.append((f'{mode_index}-{destination}-{source}',[word(destination,mode[source])]))
                other = good.shadow['modes'][1-mode_index]
                cases.append((f'{mode_index}-opposite-mode',[word('sunVisibilityOffset',other['sunVisibilityOffset'])]))
            elif scenario=='malformed-receivers':
                first = mode['receiverFacesOffset']
                receiver = mode['receiverFaces'][0]
                changes = dict(dfaceIndex=0xffffffff,modelIndex=0xffffffff,lightingOffset=0xffffffff,
                               firstSunVisibility=1,luxelW=0,luxelH=0,numChannels=2,numStyles=0,
                               flags=4,firstTriangle=1,triangleCount=0,reserved=1)
                for key,value in changes.items():
                    cases.append((f'{mode_index}-{key}',receiver_patch(
                        first+RECEIVER_KEYS.index(key)*4,struct.pack('<I',value))))
                for label,offset in (('affine',first+48),('plane',first+80),
                                     ('triangle-position',mode['receiverTrianglesOffset']),
                                     ('triangle-luxel',mode['receiverTrianglesOffset']+36)):
                    cases.append((f'{mode_index}-{label}',receiver_patch(offset,struct.pack('<f',float('nan')))))
                if mode['receiverFaceCount']>1:
                    cases.append((f'{mode_index}-duplicate-face',receiver_patch(
                        first+SHADOW_FACE_STRIDE,struct.pack('<I',receiver['dfaceIndex']))))
                cases.append((f'{mode_index}-missing-masks',[word('sunVisibilityOffset',0),word('sunVisibilityCount',0)]))
                cases.append((f'{mode_index}-reserved2',[word('reserved2',1)]))
            elif scenario=='malformed-padding':
                end = mode['sunVisibilityOffset']+mode['sunVisibilityCount']
                if end%4:
                    cases.append((f'{mode_index}-padding',[(base+end,b'\x01')]))
            elif scenario!='unknown-version':
                raise AssertionError(scenario)
        if scenario=='unknown-version':
            for version in (2,4):
                cases.append((str(version),[(sidecar['directoryOffset']+6,struct.pack('<H',version))]))
        if scenario=='malformed-padding' and not cases:
            # Create a nonzero pad byte in a truncated scalar section. Prefix
            # totals also become invalid; neither condition may be adopted.
            mode = good.shadow['modes'][0]
            count = mode['sunVisibilityCount']-1
            offset = mode['sunVisibilityOffset']+count
            payload = b''.join(sidecar['data'][mode[key]:mode[key]+mode[n]*stride] for key,n,stride in (
                ('receiverFacesOffset','receiverFaceCount',SHADOW_FACE_STRIDE),
                ('receiverTrianglesOffset','receiverTriangleCount',SHADOW_TRIANGLE_STRIDE)))
            payload += sidecar['data'][mode['sunVisibilityOffset']:offset]
            header = base+16
            cases.append(('truncated-padding',[
                (header+MODE_KEYS.index('sunVisibilityCount')*4,struct.pack('<I',count)),
                (header+MODE_KEYS.index('receiverDataCRC32')*4,struct.pack('<I',crc(payload))),
                (base+offset,b'\x01')]))
        require(cases,'no real corruption cases generated')
        for label,patches in cases:
            path = self.root/f'rejection-{scenario}-{label}.bsp'
            good.patch(path,patches)
            self.reject(path,INVALID)

    def emitter_defaults(self):
        for variant in ('sun-only', 'spot-only', 'opted-point-only'):
            bsp = self.bake(variant)[0]
            for mode in range(2):
                for r in bsp.shadow['modes'][mode]['lights']:
                    if r['light']['type']==3:
                        require(abs(r['shadowSunAngularRadius']-.27)<1e-6 and r['shadowSourceRadius']==0,'missing sun size default')
                    else:
                        require(r['shadowSunAngularRadius']==0 and r['shadowSourceRadius']==4,'missing local size default')

    def emitter_zero(self):
        for variant in ('sun-spread-0', 'shadow-radius-zero'):
            bsp = self.bake(variant)[0]
            for mode in range(2):
                require(all(r['shadowSunAngularRadius']==r['shadowSourceRadius']==0 for r in bsp.shadow['modes'][mode]['lights']),'authored zero not retained')
        require(self.runtime()['pcss']['zeroSizeIdentical'] is True, 'zero-size PCSS differs from PCF actual pixels')

    def emitter_invalid(self):
        for variant in ('sun-spread-invalid', 'sun-spread-invalid-infinity', 'sun-spread-invalid-negative', 'sun-spread-invalid-upper', 'shadow-radius-invalid', 'shadow-radius-invalid-nan', 'shadow-radius-invalid-infinity', 'shadow-radius-invalid-junk'):
            path = self.root / ('invalid-' + variant + '.bsp')
            shutil.copyfile(self.compile(variant), path)
            self.reject(path, INVALID_SIZE)

    def multi_env_last_wins(self):
        bsp = self.bake('multi-env-last-wins')[0]
        for mode in range(2):
            sun = bsp.shadow['modes'][mode]['sunLightIndex']
            require(sun==0,'missing first selected sun')
            rec = bsp.shadow['modes'][mode]['lights'][sun]
            require(abs(rec['shadowSunAngularRadius']-.53)<1e-6,'last-authored SunSpreadAngle not retained')

    def emitter_transport(self):
        sun = [self.bake(variant)[0] for variant in ('sun-only','sun-spread-0p27','sun-spread-0')]
        for mode in range(2):
            records = [bsp.shadow['modes'][mode] for bsp in sun]
            require(all(rec['sunLightIndex']==0 for rec in records),'missing selected sun')
            sizes = [rec['lights'][0]['shadowSunAngularRadius'] for rec in records]
            require(sizes[0]==sizes[1] and abs(sizes[0]-.27)<1e-6,
                    'absent and explicit 0.27 must share runtime sun size')
            require(sizes[2]==0,'explicit zero runtime sun size not retained')
        # SunSpreadAngle is also a transport key: the sun controls may differ.
        for second,radius in (('shadow-radius-zero',0),('shadow-radius-eight',8)):
            x,y = self.bake('spot-only')[0],self.bake(second)[0]
            vhv_x = {name:raw for name,raw in x.pak.items() if name.endswith('.vhv')}
            vhv_y = {name:raw for name,raw in y.pak.items() if name.endswith('.vhv')}
            require(vhv_x and vhv_x==vhv_y,'local shadow radius changed VHV')
            for mode in range(2):
                a = self.diagnostics('spot-only', True, mode=mode)
                b = self.diagnostics(second, True, mode=mode)
                require(a['sourceRadiance']==b['sourceRadiance'],'local shadow radius changed transport RGB')
                require(x.lumps[MODE_LUMPS[mode][0]]==y.lumps[MODE_LUMPS[mode][0]],
                        'local shadow radius changed receiver RGB')
                require(x.worldlights(mode)==y.worldlights(mode),'local shadow radius rewrote ordinary worldlights')
                lights_x,lights_y = x.shadow['modes'][mode]['lights'],y.shadow['modes'][mode]['lights']
                require(len(lights_x)==len(lights_y) and lights_x,'local shadow light count')
                for first,second_light in zip(lights_x,lights_y):
                    require(first['shadowSourceRadius']==4 and second_light['shadowSourceRadius']==radius,
                            'authored local runtime source radius not retained')
                    require({key:value for key,value in first.items() if key!='shadowSourceRadius'}==
                            {key:value for key,value in second_light.items() if key!='shadowSourceRadius'},
                            'local source radius changed another selected-light field')

    def overflow(self):
        bsp = self.bake('overflow-72-spots')[0]
        for mode in range(2):
            require(len(bsp.shadow['modes'][mode]['lights']) == 72, 'overflow dropped selected spot')
        report = self.runtime()
        require(report['counters']['pagesPCF'] >= 2 and report['counters']['pagesPCSS'] >= 2, '72 spots did not span runtime pages')

    def calibration_case(self, name):
        if 'receiver-inputs' not in self.cache:
            self.receiver_inputs()
        cases = self.runtime().get('receiverCases', [])
        for mode in ('ldr','hdr'):
            control = next(c for c in self.cache['receiver-inputs'] if c['name']==name and c['mode']==mode)
            case = next((c for c in cases if c['name']==name and c['mode']==mode),None)
            require(case is not None,f'missing real {mode}/{name} GPU receiver readback')
            expected = [control['ordinaryRGB'][i]*control['tint'][i]*control['styleScale'] for i in range(3)]
            require(max(expected)>0,'runtime calibration control black')
            require(len(case['runtimeRGB'])==3 and len(case['ordinaryRGB'])==3,'GPU calibration RGB shape')
            for i,(x,y) in enumerate(zip(case['runtimeRGB'],expected)):
                q = control['quantization'][i]*control['tint'][i]*control['styleScale']
                require(abs(case['ordinaryRGB'][i]-y)<=q+1e-7,'smoke control disagrees with actual BSP consumer transform')
                require(math.isfinite(x) and abs(x-y)<=.02*max(abs(x),abs(y))+q+1e-7,f'GPU direct calibration {mode}/{name}/{i}')
            if name == 'tinted':
                require(case['tint']==list(control['tint']),'tint proof lacks authored material tint')
            if name == 'style32':
                require(case['styleScale']==control['styleScale'],'lightstyle proof lacks authored non-unit style')

    def runtime_calibration(self):
        for name in ('neutral','tinted','style32','point','wide-spot'):
            self.calibration_case(name)

    def detail_initialization(self):
        cases = self.runtime().get('detailCases', [])
        for fast in (False, True):
            require(any(c['fast'] is fast and c['matched'] is True and c['count'] > 0 for c in cases), 'missing fast/ordinary detail initialization proof')

    def casters(self):
        cases = self.runtime().get('casterValidation', [])
        require(cases and all(c['mismatchCount'] == 0 and c['reportedCount'] == c['exhaustiveCount'] for c in cases), 'caster set mismatches exhaustive registrations')
        require(any(c['reportedCount'] >= 4097 and c['exceeds4096'] == 1 for c in cases), '4097 client/static renderables not actually enumerated')

    # Smoke fixture geometry: sun map 4096 texels / 4060 useful over 128 wu; sun edges start at 32 wu because a
    # 16 wu separation (2.4 shadow texels) is narrower than one 0.125 wu receiver pixel (screen-undersampled).
    SUN_SEPARATIONS = (32,64,128)
    LOCAL_SEPARATIONS = (16,64,128)
    SUN_TEXEL_WU = 128/4060
    PCSS_MAX_TEXELS = 16

    def pcss_radii(self):
        pcss = self.runtime()['pcss']
        for separation in self.SUN_SEPARATIONS:
            case = next((r for r in pcss['sun'] if r['separation'] == separation), None)
            require(case is not None, 'missing sun separation GPU readback')
            expected = min(separation*math.tan(math.radians(.27)), self.PCSS_MAX_TEXELS*self.SUN_TEXEL_WU)
            # The radius diagnostic travels as filterRadius/16 in a half-float target: ~0.1% precision plus texel-center blocker means.
            require(abs(case['radiusWorld']-expected) <= max(1e-5, expected*2.5e-3), 'sun PCSS analytic radius (capped at DX12_SHADOW_PCSS_MAX_TEXELS)')
        for z in self.LOCAL_SEPARATIONS:
            case = next((r for r in pcss['local'] if r['blockerZ'] == z), None)
            require(case is not None, 'missing local blocker GPU readback')
            expected = 4*z/(256-z)
            # Blockers are point-loaded at texel centers of a 16-sample disk: the mean blocker distance deviates from the
            # idealized plane formula by <1% at the nearest separation (smoke asserts <= 0.12 shadow texel absolute).
            require(abs(case['radiusWorld']-expected) <= max(1e-5, expected*1e-2), 'local PCSS analytic radius')

    def pcss_contact(self):
        edges = self.runtime()['pcss']['edgeWidth1090']
        for light, separations in (('sun', self.SUN_SEPARATIONS), ('local', self.LOCAL_SEPARATIONS)):
            values = sorted((c for c in edges if c.get('light', 'sun') == light), key=lambda c:c['separation'])
            require([v['separation'] for v in values]==list(separations) and all(math.isfinite(v['width']) and v['width']>0 for v in values),
                    'missing real %s %s 10-90%% edge widths' % (light, '/'.join(map(str, separations))))
            require(all(b['width']>a['width'] for a,b in zip(values,values[1:])),'%s blur does not widen with separation: not contact hardening' % light)

    def pcss_depth(self):
        values = self.runtime()['pcss']
        for key in ('noBlockersFullyLit','centerBlockerDetected','slopedNoAcne','clearNotAveraged'):
            require(values[key] is True, 'PCSS actual depth proof failed: '+key)
        require(values['seamContamination'] is False, 'foreign atlas/cube/cascade sample')
        require(math.isfinite(values['linearizationMaxErr']) and values['linearizationMaxErr'] <= .005, 'far/near/seam CPU reference relative D32 error exceeds 0.5%')

    def pcss_residency(self):
        counters = self.runtime()['counters']
        for key in ('staticRerenders','pages','casters'):
            require(counters[key+'PCF'] == counters[key+'PCSS'], 'filter switch changed static rerenders or residency: '+key)

    def run(self, name):
        corruptions = ('malformed-counts','malformed-offsets','malformed-crcs','unknown-version',
                       'malformed-overlap','malformed-receivers','malformed-padding')
        if name in corruptions:
            return self.corrupt(name)
        methods = {'sun-zero-intensity':lambda:self.sun_visibility(True), 'overflow-72-spots':self.overflow,
                   'casters-4097':self.casters, 'pcss-contact-hardening':self.pcss_contact}
        if name in methods:
            return methods[name]()
        return getattr(self, name.replace('-','_'))()


def compare_gamma_samples(a,b,label):
    require(len(a)==len(b) and a, label+': sample count')
    for va,vb in zip(a,b):
        for x,y in zip(va,vb):
            nx,ny=round(max(x,0)**(1/2.2)*255),round(max(y,0)**(1/2.2)*255)
            qx=(min(255,nx+1)/255)**2.2-(nx/255)**2.2
            qy=(min(255,ny+1)/255)**2.2-(ny/255)**2.2
            require(abs(x-y)<=.02*max(x,y)+qx+qy+1e-7,label+': 2% + gamma-byte quantization')


def require_direct_removed(ordinary,feature,label,gamma=False):
    require(len(ordinary)==len(feature) and ordinary,label+': sample count')
    removed = 0.0
    for va,vb in zip(ordinary,feature):
        if gamma:
            a,b = va,vb
            qa = [((min(255,round(max(x,0)**(1/2.2)*255)+1)/255)**2.2-x) for x in a]
            qb = [((min(255,round(max(x,0)**(1/2.2)*255)+1)/255)**2.2-x) for x in b]
        else:
            a,b = rgbexp(va),rgbexp(vb)
            qa,qb = [quantum(va)]*3,[quantum(vb)]*3
        for x,y,qx,qy in zip(a,b,qa,qb):
            require(math.isfinite(x) and math.isfinite(y) and y<=x+qx+qy+1e-7,
                    label+': feature exceeds ordinary + quantization')
            removed += x-y
    require(removed>0,label+': no selected direct removed')


def decode_vhv(raw):
    version, checksum, flags, stride, count, meshes = unpack('<6I', raw)
    require(version == 2 and stride == 4 and meshes <= (len(raw)-40)//28, 'VHV header')
    values = []
    for mesh in range(meshes):
        lod, n, offset = unpack('<3I', raw, 40 + mesh*28)
        require(offset+n*stride <= len(raw), 'VHV mesh range')
        for at in range(offset, offset+n*stride, stride):
            values.append(tuple((x/255)**2.2 for x in reversed(raw[at:at+3])))
    require(len(values) == count, 'VHV vertex count')
    return values


def decode_ppl(raw):
    version, checksum, fmt, meshes = unpack('<4I', raw)
    require(meshes <= (len(raw)-32)//32, 'PPL header')
    # IMAGE_FORMAT_RGB888=2, RGBA8888=0, RGB323232F=28; accept the writer's actual format only.
    strides = {0:4, 2:3, 28:12}
    require(fmt in strides, f'unsupported PPL image format {fmt}')
    stride = strides[fmt]
    values = []
    for mesh in range(meshes):
        lod, offset, size, width, height = unpack('<5I', raw, 32+mesh*32)
        require(size == width*height*stride and offset+size <= len(raw), 'PPL mesh range')
        for at in range(offset, offset+size, stride):
            values.append(unpack('<3f',raw,at) if fmt == 28 else tuple((x/255)**2.2 for x in raw[at:at+3]))
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--fixture-dir', required=True, type=Path)
    parser.add_argument('--tool-game', required=True, type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--only', action='append', choices=SCENARIOS, help='select one scenario; repeat for a subset')
    parser.add_argument('--runtime-report', type=Path, action='append', help='real smoke or game report JSON; repeat to merge independent readbacks')
    args = parser.parse_args()
    try:
        config = prepare(args.fixture_dir, args.tool_game)
        if args.prepare_only:
            print(f'PREPARED {args.fixture_dir.resolve()}\nTOOL GAME {args.tool_game.resolve()}\nSTATIC MODEL {config["model"]}')
            return 0
        runner = Runner(args, config)
    except (AssertionError, OSError, ValueError, KeyError, IndexError, struct.error, zipfile.BadZipFile, lzma.LZMAError) as error:
        print(f'FAIL prerequisites: {error}', file=sys.stderr)
        return 1
    results = []
    for name in args.only or SCENARIOS:
        try:
            runner.run(name)
            results.append((name,'PASS',''))
        except (AssertionError, OSError, ValueError, KeyError, IndexError, struct.error, zipfile.BadZipFile, lzma.LZMAError) as error:
            results.append((name,'FAIL',str(error)))
    print('\nSCENARIO                       RESULT  EVIDENCE / FAILURE')
    print('-'*90)
    for name, status, detail in results:
        print(f'{name:30} {status:6}  {detail}')
    return int(any(status == 'FAIL' for _, status, _ in results))


if __name__ == '__main__':
    raise SystemExit(main())
