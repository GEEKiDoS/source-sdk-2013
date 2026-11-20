"""Extract the backend's coverage bits from the stdshader model PS index ABI."""
import re
import sys
from pathlib import Path


def coverage_header(combo_include):
    # Use the generated GetIndex expression, not shader declaration order: it
    # already includes the dynamic combo count and every preceding static axis.
    static_class = combo_include.split('class pbr_model_ps51_Static_Index\n{', 1)[1].split('\n};', 1)[0]
    expression = re.search(r'\breturn ([^;]+);', static_class).group(1)
    lines = ['// Generated from pbr_model_ps51_Static_Index::GetIndex().', '#pragma once']
    for axis, name in [('ALPHATEST', 'AlphaTest'), ('INFECTED', 'Infected')]:
        stride, = re.findall(r'\(\s*(\d+)\s*\*\s*m_n' + axis + r'\s*\)', expression)
        lines.append(f'constexpr int kModel{name}Stride = {stride};')
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    Path(sys.argv[2]).write_text(coverage_header(Path(sys.argv[1]).read_text()), encoding='ascii')
