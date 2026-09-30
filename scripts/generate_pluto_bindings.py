"""Generate an auditable relocation table for the pinned public Pluto DLL.
Dependencies: pefile, capstone. The input DLL is never modified.
"""
import argparse, hashlib, pathlib, struct
import capstone, pefile
p=argparse.ArgumentParser();p.add_argument('dll',type=pathlib.Path);p.add_argument('out',type=pathlib.Path);args=p.parse_args()
expected='7e360b643c8c0156c03fe0cad9972a3058138ccfe22f921c5b4e0cd0aaf0abef'
raw=args.dll.read_bytes()
if hashlib.sha256(raw).hexdigest()!=expected:raise SystemExit('Unsupported Pluto release; SHA-256 mismatch')
pe=pefile.PE(data=raw);base=pe.OPTIONAL_HEADER.ImageBase
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);cs.detail=True;cs.skipdata=True
single={0x485a40,0x512688,0x59688c,0x596904,0x6283ec,0x6283f4,0x628430,0x64dec4,0x654880,0x654aa0,0x6d1260}
def known(v):return v in single or 0x5124d8<=v<0x512510 or 0x57eee0<=v<0x5967f0 or 0x59cca8<=v<0x59cdf8
rows=[]
for sec in pe.sections:
 if not sec.Characteristics&0x20000000:continue
 for ins in cs.disasm(sec.get_data(),base+sec.VirtualAddress):
  if not ins.id:continue
  for op in ins.operands:
   if op.type==capstone.x86.X86_OP_MEM:val,off,size=op.mem.disp,ins.disp_offset,ins.disp_size
   elif op.type==capstone.x86.X86_OP_IMM and ins.mnemonic not in ('call','jmp'):val,off,size=op.imm,ins.imm_offset,ins.imm_size
   else:continue
   signed=(val+0x80000000)%0x100000000-0x80000000
   if not known(abs(signed)):continue
   assert size==4
   expected_word=val&0xffffffff
   assert struct.unpack_from('<I',ins.bytes,off)[0]==expected_word
   rows.append(f'  {{0x{ins.address-base+off:x},0x{expected_word:08x}}}, // {ins.address-base:x}: {ins.mnemonic} {ins.op_str}')
args.out.write_text('#pragma once\n#include <cstdint>\n// Generated for Pluto cog2026-2578600. Do not apply to another release.\nstruct PlutoBinding {uint32_t rva;uint32_t expected;};\ninline constexpr PlutoBinding pluto_bindings[]={\n'+'\n'.join(rows)+'\n};\n')
print(f'{len(rows)} verified address operands')
