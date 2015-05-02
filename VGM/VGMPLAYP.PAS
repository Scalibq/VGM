program vgmplayp;
{
Plays a Sega Master System, "PSG", or other SN76489/SN76496 .VGM on an
IBM PCjr or Tandy 1000.  trixter@oldskool.org, 20130404

This is an extremely simple player that does not handle all cases, but
should play most files.  Multiple tunes per file are ignored.  Looping
is ignored.  Future enhancements may allow this, but don't hold your breath.

This is the "polling" version of the player that busywaits each
timeslice.  Look at other code if you want the interrupt-driven player.
This player is an example of how to interpret and play files in a simple,
easy-to-understand way.

Speed is a little slower than it should be; I'm not sure why.
Problem is in tickwait() somewhere.
If this bothers you, change all timewait durations from 60Hz to about 65Hz.
}

uses
  support,
  TInterrupts,
  cmdlin;

type
  TVGMheader=record
    VGMIdent,EOFoffset,Version,SN76489Clock,
    YM2413clock,GD3offset,totalSamples,loopOffset,
    loopNumSamples,Rate:longint;
    SNFB:word;
    _SNW,_SF:byte;
    YM2612clock,YM2151clock,VGMdataoffset,SegaPCMclock,SPCMInterface,
    RF5C68clock,YM2203clock,YM2608clock,YM2610Bclock,
    YM3812clock,YM3526clock,Y8950clock,YMF262clock,
    YMF278Bclock,YMF271clock,YMZ280Bclock,RF5C164clock,
    PWMclock,AY8910clock:longint;
    _AYT:byte; AYFlags:array[0..2] of byte;
    _VM,reserved1,_LB,_LM:byte;
    GBDMGclock,NESAPUclock,MultiPCMclock,uPD7759clock,
    OKIM6258clock:longint;
    _OF,_KF,_CF,reserved2:byte;
    OKIM6295clock,K051649clock,K054539clock,HuC6280clock,C140clock,
    K053260clock,Pokeyclock,QSoundclock,reserved3,reserved4:longint;
  end;
  PVGMheader=^TVGMheader;

  tbytes=array[0..65536-16-1] of byte;
  pbytes=^tbytes;

const
  VFileIdent=$206d6756;
  SampleRate=44100;
  playing:boolean=true;

var
  f:file;
  VGM:pVGMheader;
  buf:pointer;
  ba:pbytes;
  idx:word;  {index into byte array}
  w:word;
  cmd:byte;
  c:char;

const
  SNReg=$C0;
  SNFreq=3579540;
  SNMplxr=$61; {MC14529b sound multiplexor chip in the PCjr}

Procedure InitPCjrAudio;
begin
  asm
    in  al,SNMplxr
    or  al,01100000b {set bits 6 and 5 to route SN audio through multiplexor}
    out SNMplxr,al
  end;
end;

Procedure SetPCjrAudio(chan:byte;freq:word;volume:byte); {volume is 0-15}
{Sets an SN voice with volume and a desired frequency}
var
  period:integer;
begin
  period := SNFreq div (32*freq);
  {clamp period so that it doesn't exceed invalid ranges.  This also
  removes the need to strip out bits that would interfere with the
  OR'd command bits sent to the register}
  if period > 1023 then period:=1023;
  if period < 0 then period:=0;
{
  To set a channel, we first send frequency, then volume.
  Frequency:
  76543210 76543210
  1                 - set bit to tell chip we are selecting a register
   xx0              - set channel.  4, 2, and 0 are valid values.
      xxxx          - low 4 bits of period
           0        - clear bit to tell chip more freq. coming
            x       - unused
             xxxxxx - least sig. 6 bits of period

  Sending a word value will not work on PCjr, so send bytes individally.
  (It does work on Tandy, but we want to be nice.)

  Set attenuation (volume):

  76543210
  1                 - set bit to tell chip we are selecting a register
   xx1              - register number (valid values are 1, 3, 5, 7)
      xxxx          - 4-bit volume where 0 is full volume and 15 is silent)

}
  asm
    {this procedure could be optimized to be either smaller OR faster
    but I just want to get it working right now}
    {build MSB}
    mov al,chan
    add al,al            {voice doubled = register #}
    mov cl,4
    shl al,cl            {get voice reg in place}
    or  al,10000000b     {tell chip we are selecting a reg}
    mov dx,period        {save period val for later}
    mov bx,dx
    and bl,00001111b     {grab least sig 4 bits of period...}
    or  al,bl            {and put them in MSB}
    out SNreg,al         {output MSB}
    {build LSB}
    mov bx,dx            {restore original period val}
    shr bx,cl            {isolate upper 6 bits}
    and bl,01111111b     {clear bit 7 to indicate rest of freq}
    mov al,bl
    out SNreg,al         {send LSB}

    {set the volume}
    mov al,chan
    inc al
    add al,al
    dec al               {set voice 3 bits to 1, 3, 5, or 7}
    shl al,cl            {get voice value into place}
    or  al,10000000b     {tell chip we're selecting a reg}
    mov bl,volume
    not bl               {adjust to attenuation; register expects 0 = full, 15 = quiet}
    and bl,00001111b     {mask off junk bits}
    or  al,bl            {merge the volume into the reg select bits}
    out SNreg,al         {send volume}
  end;
end;

Procedure ClosePCjrAudio;
var
  chan:byte;
begin
  for chan:=0 to 2 do SetPCjrAudio(chan,440,0);
  {Reset the multiplexor}
  asm
    in  al,SNMplxr
    and al,10011100b {clear 6 and 5 to route PC speaker through multiplexor; 1 and 0 turn off timer signal}
    out SNMplxr,al
  end;
end;

Procedure tickWait(numticks:word); assembler;
{
Waits for numticks to elapse, where a tick is 1/PIT Frequency (~1193192)
This procedure is unoptimized; todo: everything in regs
}
asm
  {Build PIT command: Channel 0, Latch Counter, Rate Generator, Binary}
  mov    bh,iMC_Chan0+iMC_LatchCounter+iMC_OpMode2+iMC_BinaryMode
  mov    al,bh
  {get initial count}
  cli
  out    43h,al         {Tell timer about it}
  in     al,40h         {Get LSB of timer counter}
  xchg   al,ah          {Save it in ah (xchg accum,reg is 3c 1b}
  in     al,40h         {Get MSB of timer counter}
  sti
  xchg   al,ah          {Put things in the right order; AX:=starting timer}
  mov    dx,ax          {store for later}

@wait:
  {get next count}
  mov    al,bh          {Use same Mode/Command as before (latch counter, etc.)}
  cli                   {Disable interrupts so our operation is atomic}
  out    43h,al         {Tell timer about it}
  in     al,40h         {Get LSB of timer counter}
  xchg   al,ah          {Save it in ah for a second}
  in     al,40h         {Get MSB of timer counter}
  sti
  xchg   al,ah          {AX:=new timer value}
  {see if our count period has elapsed}
  mov    si,dx          {copy original value to scratch}
  sub    si,ax          {subtract new value from old value}
  cmp    si,numticks    {compare si to maximum time allowed}
  jb     @wait          {if still below, keep waiting; if above, blow doors}
end;

Procedure printHelp;
begin
  writeln('Usage: ',paramstr(0),' filename.vgm');
end;

begin
  if non_flag_count<1 then begin
    printHelp;
    halt(1);
  end;
  assign(f,non_flag_param(1));
  reset(f,1);
  if filesize(f)>65536-16
    then fatalerror(2,'VGM too big, must be under 64K');
  getmem(buf,filesize(f));
  blockread(f,buf^,filesize(f));
  close(f);
  VGM:=buf;
  pointer(ba):=pointer(VGM);

  {File appears sane?}
  with VGM^ do begin
    if VGM^.VGMIdent<>VFileIdent
      then fatalerror(3,'Header of '+hex(VGM^.VGMIdent)+' does not appear to be a VGM file');
    writeln(non_flag_param(1),' details:');
    writeln('EoF Offset: ',EOFoffset);
    writeln('Version: ',hex(Version));
    writeln('GD3 Offset: ',GD3offset);
    writeln('Total # samples: ',totalSamples);
    writeln('Playback Rate: ',Rate);

    writeln('VGM Data Offset: ',vgmDataOffset);
    if vgmDataOffset=0 then idx:=$40 else idx:=vgmDataOffset+$34;
    writeln('VGM Data starts at 0x',hex(idx));
    {Can we play this on PCjr hardware?}
    if SN76489Clock<>0
      then writeln('SN76489 Clock: ',SN76489clock,' Hz')
      else fatalerror(3,'File does not contain data for our hardware');

    writeln('Hit a key to begin playback');
    readkeychar;

    {Print GD3 tag to be nice}
    writeln(#13#10'GD3 Tag Information:');
    for w:=GD3Offset+$18 to EOFoffset-$4 do begin
      c:=char(ba^[w]);
      case c of
        #32..#127:write(c);
        #0:if ba^[w+1]=0 then writeln; {see goofy GD3 spec for details}
      end;
    end;
    writeln;
  end;

  {Start playing.  Use polling method as we are not trying to be fancy
  at this stage, just trying to get something working}

  InitPCJrAudio;
  {SetPCjrAudio(1,440,15); readln;}

  {init PIT channel 0, 3=access mode lobyte/hibyte, mode 2, 16-bit binary}
  {We do this so we can get a sensible countdown value from mode 2 instead
  of the 2xspeedup var from mode 3.  I have no idea why old BIOSes init
  mode 3; everything 486 and later inits mode 2.  Go figure.  This should
  not damage anything in DOS or TSRs, in case you were wondering.}
  InitChannel(0,3,2,$0000);

  while playing do begin
    cmd:=ba^[idx];
    case cmd of
      $4f:inc(idx,2); {stereo PSG cmd, ignored}
      $50:begin {PSG data write}
            inc(idx); {advance to data position}
            port[SNReg]:=ba^[idx];
            inc(idx); {advance to next command}
          end;
      $61:begin {wait n samples}
            write('v'); {indicate we're doing a multiframe/variable wait}
            inc(idx); {advance to data position}
            w:=ba^[idx] OR (ba^[idx+1] SHL 8);
            {max reasonable tickwait time is 50ms, so handle larger values in slices}
            if w > (samplerate div 20) then repeat
              tickwait(PITfreq div 20); {wait 1/20th}
              dec(w,44100 div 20) {reduce total wait value by time we waited}
            until w <= (samplerate div 20);
            tickwait(PITFreq div (samplerate div w)); {handle only or remaining slice}
            inc(idx,2); {advance to next command}
          end;
      $62:begin {wait 1/60th second}
            tickwait(PITfreq div 60);
            inc(idx);
          end;
      $63:begin {wait 1/50th second}
            tickwait(PITfreq div 50);
            inc(idx);
          end;
      $66:playing:=false; {end of VGM data}
    else begin
           write(#13,'Invalid: ',hex(ba^[idx]));
           inc(idx);
         end;
    end; {case}

    {handle input}
    if keypressed then case readkeychar of
      #27:playing:=false;
    end;

  end; {while playing}

  ClosePCjrAudio;
end.

