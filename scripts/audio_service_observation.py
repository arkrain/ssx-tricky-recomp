"""Optional observation around the supported game's streaming buffer service."""
from pathlib import Path

def apply(root):
    path=Path(root)/'src/recomp/gen/recomp_0000.c'
    text=path.read_text()
    begin=text.index('void sub_00015110(void)')
    end=text.index('/**\n * sub_000151F0',begin)
    body=text[begin:end]
    for marker,code in [('loc_00015128: ;\n','    ssx_audio_service_trace(0, RECOMP_READ32(esp + 8));\n'),('loc_000151E0: ;\n','    ssx_audio_service_trace(1, esi);\n')]:
        if marker+code not in body:
            assert body.count(marker)==1
            body=body.replace(marker,marker+code)
    declaration='    extern void ssx_audio_service_trace(unsigned, unsigned);\n'
    if declaration not in body:body=body.replace('void sub_00015110(void)\n{\n','void sub_00015110(void)\n{\n'+declaration,1)
    marker='    RECOMP_STORE32(0x205BE8, esi);\n'
    code='    ssx_audio_service_publish(esi);\n'
    if marker+code not in body:
        assert body.count(marker)==1
        body=body.replace(marker,marker+code)
    declaration='    extern void ssx_audio_service_publish(unsigned);\n'
    if declaration not in body:body=body.replace('void sub_00015110(void)\n{\n','void sub_00015110(void)\n{\n'+declaration,1)
    text=text[:begin]+body+text[end:]
    begin=text.index('void sub_00014DF0(void)')
    end=text.index('/**\n * sub_00015010',begin)
    body=text[begin:end]
    for address,name in [('205BE4','lead'),('205BE0','ring')]:
        marker=f'    RECOMP_STORE32(0x{address}, edx);\n'
        code=f'    edx = ssx_audio_stream_{name}(edx);\n'
        if code+marker not in body:
            assert body.count(marker)==1
            body=body.replace(marker,code+marker)
        declaration=f'    extern unsigned ssx_audio_stream_{name}(unsigned);\n'
        if declaration not in body:
            body=body.replace('void sub_00014DF0(void)\n{\n','void sub_00014DF0(void)\n{\n'+declaration,1)
    text=text[:begin]+body+text[end:]
    path.write_text(text)
