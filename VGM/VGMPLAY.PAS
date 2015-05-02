{$O-,F+}
unit vgmplay;
{
Plays a Sega Master System, "PSG", or other SN76489/SN76496 .VGM on an
IBM PCjr or Tandy 1000.  trixter@oldskool.org, 20130413

This is an extremely simple unit meant for background playing of a VGM
on a PCjr or slow Tandy.  Only four commands are supported: data, pause 1/60,
pause 1/50, and GameGear stereo (which is ignored).  All others are ignored,
so if your file doesn't play properly, try the polling player vgmplayp
instead.  If you have optimized your file with "vgmtools" then you likely
have variable-length pauses in your file which are not currently supported
in this unit.

Multiple tunes per file are ignored.  Variable-length pauses are ignored.
Looping is ignored.  VGM files must be uncompressed and yet fit into
64K.  Future enhancements may bypass these limitations.

Written and tested on a PCjr.  If it bugs out on other platforms, let me know.

Basic design for interrupt-driven playing:

- VGMPlayFrame plays a single "frame" of VGM data (everything until a 1/60th
or 1/50th pause).  It requires no setup, so that it can be called manually
by a user program if they choose to hook it into their interrupt system.
It honors VGMPlaying (if not playing, then we're stopped and must VGMInit
again).

- VGMInt is the interrupt shell called by the PIT that calls VGMPlayFrame.
It honors VGMpaused; if paused, it will not call VGMPlayFrame.
}

interface

const
  VFileIdent=$206d6756;
  VGMplaying:boolean=true;
  VGMpaused:boolean=true;
  VGMLooping:word=$ffff; {if FFFF then no looping; otherwise frame to loop to}
  VGMbackground:boolean=true;
  VGMplaycount:word=0;

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

var
  VGMheader:pVGMheader;
  {$IFDEF USERPROC}
  VGMuserProc:procedure;
  {$ENDIF}

procedure VGMinit(vgmdata:pointer;background:boolean;loopPos:word);
procedure VGMPlayFrame;
procedure VGMclose;
procedure VGMdummyProc;

implementation

uses
  {$IFDEF PROFILE}
  m6845ctl,
  {$ENDIF}
  PCjrSupport,
  support,
  TInterrupts;

const
  SampleRate=44100;
  {$IFDEF USERPROC}
  userProcActive:boolean=false;
  {$ENDIF}

type
  tbytes=array[0..65536-16-1] of byte;
  pbytes=^tbytes;

var
  {ba:pbytes;}
  idx,loopidx:word;  {indexes into data}

Procedure VGMdummyProc; assembler;
asm
  nop
end;

(*Procedure VGMPlayFrame;
{Plays a single "frame" (1/60th or 1/50th) of VGM data}
var
  cmd:byte;
begin
  if VGMplaying then inc(VGMPlayCount);
  while VGMplaying do begin
    cmd:=ba^[idx];
    case cmd of
      $4f:inc(idx,2); {stereo PSG cmd, ignored}
      $50:begin {PSG data write}
            inc(idx); {advance to data position}
            port[SNReg]:=ba^[idx];
            inc(idx); {advance to next command}
          end;
      $62:begin {wait 1/60th second}
            inc(idx);
            break; {done with frame, stop processing}
          end;
      $63:begin {wait 1/50th second}
            inc(idx);
            break; {done with frame, stop processing}
          end;
      $66:VGMplaying:=false; {end of VGM data}
    else  begin
            inc(idx); {bad or unsupported data, move on}
          end;
    end; {case}
  end;
end;*)

Procedure VGMPlayFrame; assembler;
{
Plays a single "frame" (1/60th or 1/50th) of VGM data -- IN AZM BITCHES
Can probably be sped up by using a jump table, if you want to improve it.
}
asm
  cmp     VGMPlaying,1    {1=true, 0=false}
  jne     @finalexit      {if not, bail}
  mov     di,idx
  mov     dx,VGMPlayCount
  cmp     dx,VGMLooping   {are we at the frame we're looping at?}
  jne     @continueinit   {if not, keep going}
  mov     loopidx,di      {otherwise, make a note of what idx we're at}
@continueinit:
  mov     dx,SNReg        {set up for port writes}
  mov     cx,$0020        {use a counter to stop inf. loop if fed bad data}
  cld
  push    ds
  lds     si,VGMHeader
  add     si,di           {ds:si now points to current VGM data}

@frameloop:
  lodsb                   {load next command byte}
  cmp     al,$50          {write data command?}
  je      @writedata
  cmp     al,$62          {wait 1/60th?}
  je      @done
  cmp     al,$63          {wait 1/50th?}
  je      @done
  cmp     al,$66          {end of song data?}
  je      @stopsong
  loop    @frameloop      {none of these?  Keep processing}
  jmp     @done           {if we got here then data is mangled; exit cleanly}

@writedata:
  lodsb                   {load byte to write to SN register}
  out     dx,al           {send to SN}
  jmp     @frameloop

@stopsong:
  pop     ds              {get our global data segment back}
  cmp     VGMLooping,$ffff{are we looping to some predefined index?}
  jne     @setuploop
  mov     VGMPlaying,0    {set to false}
  jmp     @finalexit      {exit}

@setuploop:
  mov     si,loopidx      {re-home to loop position}
  mov     idx,si          {idx := loopidx}
  mov     si,VGMLooping
  mov     VGMPlayCount,si {reset VGMPlayCount to the frame we're looping to}
  jmp     @finalexit      {ready to play again at new location}

@done:
  pop     ds              {get our global data segment back}
  mov     idx,si          {store current data position back into memory}
@abort:
  inc     VGMPlayCount    {increment frame count}
@finalexit:
end;

{$F+}
procedure VGMhandler; Interrupt;
begin
  if VGMPlaying and not VGMpaused then VGMPlayFrame;
  {$IFNDEF EVILINT}
  {We want to be nice, and will maintain the BIOS interrupt}
  {Maintaining the BIOS interrupt on PCjr is HIGHLY RECOMMENDED!  Okay to
  ignore on other platforms if you're ok with time drift}
    inc(PITCycles,Chan0Counter); {Keep track of how many PIT cycles have gone by}
    if longrec(PITCycles).hi <> 0 then begin {Did we roll over?  Is it time to call the 18.2Hz BIOS handler?}
      longrec(PITCycles).hi:=0; {Update our PIT cycles counter}
      asm pushf end; {simulate an interrupt by pushing flags, then CALLing handler}
      BIOSTimerHandler; {this will acknowledge the interrupt}
    end
    else
      Port[$20] := $20; {send EndOfInterrupt to the PIC to ackn. the interrupt}
  {$ELSE}
    Port[$20] := $20; {Just acknowledge the interrupt, oldint8 be damned}
  {$ENDIF}
end;

procedure VGMinit(vgmdata:pointer;background:boolean;loopPos:word);
{if loopPos is $ffff then we are not looping, otherwise provide frame
to loop to}
begin
  VGMLooping:=loopPos;
  VGMPlayCount:=0;
  VGMHeader:=vgmdata;
  {ba:=vgmdata;}
  VGMBackground:=background;
  with VGMheader^ do begin
    if VGMIdent<>VFileIdent
      then fatalerror(3,'Header of 0x'+hex(VGMIdent)+' does not appear to be a VGM file');
    if SN76489Clock=0
      then fatalerror(3,'VGM Data not for our hardware');
    if vgmDataOffset=0 then idx:=$40 else idx:=vgmDataOffset+$34;

    JrInitAudio;
    VGMPlaying:=true;
    if VGMBackground then begin
      SetTimerHz(@VGMhandler,Rate);
      VGMPaused:=false;
    end;
  end;
end;

Procedure VGMClose;
begin
  VGMPlaying:=false;
  if VGMBackground then CleanUpTimer;
  jrCloseAudio;
end;

{$IFDEF USERPROC}
begin
  VGMuserProc:=VGMdummyProc;
{$ENDIF}
end.

scratch area here, nothing gets compiled after the last "end."

{$F+}
procedure VGMhandler; Interrupt;
begin
  {$IFDEF PROFILE}
    {$IFDEF PCJR}
    asm
      {port[$3da]:=2; port[$3da]:=1;}
      mov dx,$3da
      mov al,2
      out dx,al
      dec al
      out dx,al
    end;
    {$ENDIF}
  {$ENDIF}
  if VGMPlaying and not VGMpaused then VGMPlayFrame;
  {$IFDEF USERPROC}
    {$IFDEF PROFILE}
      {$IFDEF PCJR}
      asm
        mov dx,$3da
        mov al,2
        out dx,al
        inc al
        out dx,al
      end;
      {$ENDIF}
    {$ENDIF}
    VGMuserProc;
  {$ENDIF}
  {$IFDEF PROFILE}
    {$IFDEF PCJR}
    asm
      mov dx,$3da
      mov al,2
      out dx,al
      xor ax,ax
      out dx,al
    end;
    {$ENDIF}
  {$ENDIF}

  {$IFNDEF EVILINT} {We want to be nice, and will maintain the BIOS interrupt}
  {Maintaining the BIOS interrupt on PCjr is HIGHLY RECOMMENDED!  Okay to
  ignore on other platforms if you're ok with time drift}
    inc(PITCycles,Chan0Counter); {Keep track of how many PIT cycles have gone by}
    if longrec(PITCycles).hi <> 0 then begin {Did we roll over?  Is it time to call the 18.2Hz BIOS handler?}
      longrec(PITCycles).hi:=0; {Update our PIT cycles counter}
      asm pushf end; {simulate an interrupt by pushing flags, then CALLing handler}
      BIOSTimerHandler; {this will acknowledge the interrupt}
    end
    else
      Port[$20] := $20; {send EndOfInterrupt to the PIC to ackn. the interrupt}
  {$ELSE}
    Port[$20] := $20; {Just acknowledge the interrupt, oldint8 be damned}
  {$ENDIF}
end;

