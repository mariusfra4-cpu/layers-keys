import struct
PPQ=1920; BAR=4*PPQ
ev=[]  # (tick, order, bytes)
o=[0]
def add(t,b):
    o[0]+=1; ev.append((t,o[0],bytes(b)))
def meta(t,typ,data): 
    data=data.encode() if isinstance(data,str) else bytes(data)
    add(t,[0xFF,typ]+vlq(len(data))+list(data))
def vlq(n):
    out=[n&0x7F]; n>>=7
    while n: out.insert(0,(n&0x7F)|0x80); n>>=7
    return out
def sysex(t,data): add(t,[0xF0]+vlq(len(data)+1)+data+[0xF7])
def note(t,ch,k,v,d):
    add(t,[0x90|ch,k,v]); add(t+d,[0x80|ch,k,0])
def cc(t,ch,c,v): add(t,[0xB0|ch,c,v])
def pc(t,ch,p): add(t,[0xC0|ch,p])
meta(0,6,"SFF1"); meta(0,3,"Test Pop 8Beat"); meta(0,0x51,[0x07,0xA1,0x20]); meta(0,0x58,[4,2,24,8])
sysex(0,[0x43,0x10,0x4C,0x00,0x00,0x7E,0x00])
meta(0,6,"SInt")
setup={8:(127,0,0),9:(127,0,0),10:(0,0,33),11:(0,0,4),12:(0,0,25),13:(0,0,48),14:(0,0,61),15:(0,0,73)}
for ch,(msb,lsb,p) in setup.items():
    cc(0,ch,0,msb); cc(0,ch,32,lsb); pc(0,ch,p); cc(0,ch,7,100); cc(0,ch,10,[64,64,64,40,90,64,70,50][ch-8]); cc(0,ch,91,50)
secs=[("Intro A",2),("Main A",4),("Fill In AA",1),("Fill In AB",1),("Main B",4),("Fill In BB",1),("Fill In BA",1),("Ending A",2)]
t=BAR
CM7=[60,64,67,71]
for name,bars in secs:
    meta(t,6,name)
    busy = name.startswith("Fill")
    for b in range(bars):
        bt=t+b*BAR
        # drums ch9
        for i in range(8):
            st=bt+i*PPQ//2
            note(st,9,42,90 if i%2==0 else 60,PPQ//4)
            if i in (0,4) or (busy and i in (5,7)): note(st,9,36,110,PPQ//4)
            if i in (2,6): note(st,9,38,105,PPQ//4)
        if busy:
            for i in range(8): note(bt+BAR//2+i*PPQ//4,9,[50,48,47,45][i//2],100,PPQ//8)
        if b==0: note(bt,9,49,100,PPQ)
        # bass (Main B on source ch 2 -> routed to ch 10)
        bch = 2 if name=="Main B" else 10
        for i,k in enumerate([36,36,43,36]): note(bt+i*PPQ,bch,k,100,PPQ-120)
        # chord1 piano
        for i in range(4):
            for k in CM7: note(bt+i*PPQ,11,k,80,PPQ//2)
        # chord2 guitar off-beats
        for i in range(4):
            for k in [52,55,59,64]: note(bt+i*PPQ+PPQ//2,12,k,70,PPQ//4)
        # pad
        if name.startswith("Main"):
            for k in [48,55,64]: note(bt,13,k,64,BAR-60)
        # phrase
        if name in("Intro A","Ending A"):
            for i,k in enumerate([72,74,76,79]): note(bt+i*PPQ,14,k,90,PPQ-200)
        if name=="Main B" and b==3:
            cc(bt,15,1,0)
            for i,k in enumerate([84,83,79,76]): note(bt+i*PPQ,15,k,85,PPQ)
    t+=bars*BAR
ev.sort(key=lambda e:(e[0],e[1]))
trk=b""; last=0
for tk,_,b in ev:
    trk+=bytes(vlq(tk-last))+b; last=tk
trk+=bytes([0,0xFF,0x2F,0])
smf=b"MThd"+struct.pack(">IHHH",6,0,1,PPQ)+b"MTrk"+struct.pack(">I",len(trk))+trk
def chunk(id,d): return id.encode()+struct.pack(">I",len(d))+d
def ctab(src,name,dest,ntt=1,lo=0,hi=127):
    d=bytes([src])+name.ljust(8).encode()[:8]+bytes([dest,0])+b"\x0f\xff"+b"\x1f\xff\xff\xff\xff"+bytes([0,0x22])+bytes([0,ntt,0x7F,lo,hi,0,0])
    return chunk("Ctab",d)
parts=[(8,"Rhy1",8,0),(9,"Rhy2",9,0),(10,"Bass",10,1),(11,"Chord1",11,2),(12,"Chord2",12,2),(13,"Pad",13,2),(14,"Phr1",14,1),(15,"Phr2",15,1)]
seg1=chunk("Sdec",b"Intro A,Main A,Fill In AA,Fill In AB,Fill In BB,Fill In BA,Ending A")+b"".join(ctab(s,n,d,t) for s,n,d,t in parts)
seg2=chunk("Sdec",b"Main B")+b"".join(ctab(s if s!=10 else 2,n,d,t) for s,n,d,t in parts)
casm=chunk("CASM",chunk("CSEG",seg1)+chunk("CSEG",seg2))
open("TestPop.sty","wb").write(smf+casm)
# MIDI de référence pour fluidsynth (Main A seul, sans CASM) 

