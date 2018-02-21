.8086
.Model Small

include common.inc
include cmdline.inc
include 8253.inc
include 8259A.inc
include LPT.inc
include OPL2LPT.inc
include DBS2P.inc

BUFSIZE equ 32768
NUMBUF equ 1

LPT_BASE equ 0378h

PreHeader struc
	marker 		db 4 dup(?)	; = {'P','r','e','V'}; // ("Pre-processed VGM"? No idea, just 4 characters to detect that this is one of ours)
	headerLen	dd ?		; = sizeof(_PreHeader); // Good MS-custom: always store the size of the header in your file, so you can add extra fields to the end later
	_size		dd ?		; Amount of data after header
	_loop		dd ?		; Offset in file to loop to
	version		db ?		; Including a version number may be a good idea
	nrOfSN76489	db ?
	nrOfSAA1099	db ?
	nrOfAY8930	db ?
	nrOfYM3812	db ?
	nrOfYMF262	db ?
	nrOfMIDI	db ?
PreHeader ends

LOCALS @@

.stack 100h

.code
start:
	; Retrieve commandline
    ; At program start, DOS sets DS to point to the the PSP.
	; Set ES to PSP
	push	ds
	pop		es
	
	; Set DS to data segment
	mov		ax, @data
	mov		ds, ax
	
	; Set si to cmdline struct
	mov		si, offset _cmdline
	
	call	GetCmdLine

	; Verify commandline
	cmp		ds:[_cmdline.argc], 0
    jne     @@getfname                 ;if not, get filename; else, abort

    ; Print usage message, then exit
    mov     ah, 9
    mov     dx, offset usageSt
    int     21h
    mov     ax, 4C00h
    int     21h

	; Retrieve filename from commandline
@@getfname:
	mov		si, [_cmdline.argv]
	mov		cx, [_cmdline.argv][2]
	sub		cx, si	; Calculate string length
	dec		cx		; Remove space
	mov		di, offset fileName

	; Swap PSP to DS and @data to ES
	push	es
	push	ds
	pop		es
	pop		ds
	
	rep		movsb	; Copy string to target variable
	xor		ax, ax
	stosb			; Add zero-terminator
	
	; Retrieve sample rate from commandline
	;cmp		es:[_cmdline.argc], 2
	;jb		@@endCmdLine
	
	;mov		si, es:[_cmdline.argv][2]
	;call	ParseDec
	
	; Sample rate is in Hz, convert to PIT ticks
	;mov		dx, (PITFREQ SHR 16)
	;mov		ax, (PITFREQ AND 0FFFFh)
	;div		bx
	;mov		es:[speed], ax
	
	;cmp		es:[_cmdline.argc], 3
	;jb		@@endCmdLine
	
	;mov		si, es:[_cmdline.argv][4]
	;call	ParseHex
	;mov		word ptr es:[patchPort+1], bx		; Patch LPT1 address into code
	
@@endCmdLine:	
	; Swap @data back to DS and PSP back to ES
	push	es
	push	ds
	pop		es
	pop		ds
	
	; Disable all interrupts except timer and keyboard
	;in al, PIC1_DATA
	;mov [picMask], al
	;mov al, not 03h
	;out PIC1_DATA, al

	mov		ah, 4Ah
	mov		bx, 100h	; TODO: Calculate proper size!!
	int		21h		; DOS -	2+ - ADJUST MEMORY BLOCK SIZE (SETBLOCK)
					; ES = segment address of block	to change
					; BX = new size	in paragraphs
	jnc @@noErr1
	jmp err
@@noErr1:

	; Allocate preprocessed VGM buffer of 64K (1000h paragraphs)
	mov ah, 48h
	mov bx, 1000h
	int 21h
	jnc @@noErr2
	jmp err
@@noErr2:

	mov [sampleBufSeg], ax
	mov es, ax
	
	; Open music stream file
	mov al, 0	; read-only
	mov ah, 03Dh
	mov dx, offset fileName
	int 21h
	mov cs:[file], ax
	jnc fileOpen
	
	; File did not open correctly?
    mov     ah, 9
    mov     dx, offset errorFile
    int     21h
	jmp err
	
fileOpen:
	; Process header
	call LoadHeader
	
	; Preprocess sample
REPT NUMBUF
	call LoadBuffer
ENDM
	; Reset ending-flag because we haven't even started yet.
	mov cs:[ending], 0

	; Get PIC into auto-EOI mode
	call GetMachineType
	mov [machineType], cl
	call InitAEOI
	
	; Copy handler to sample buffer
	mov cx, (EndTimerHandler-TimerHandler+1)/2
	mov si, offset TimerHandler
	mov di, 32768
	rep movsw
	
	; Turn off floppy motor so we can hear more clearly
	;mov		dx, 03F2h
	;mov		al, 0Ch
	;out		dx, al
	
	;call InitPCSpeaker
	;call InitPCjrAudio
	
	; Init OPL2
	mov dx, 0388h
	mov cx, 0F5h
	; Set all registers to 0
@@regLoop:
	mov al, cl
	out	dx, al
rept 6
    in      al,dx
endm
    inc     dx
	xor ax, ax
	out	dx, al
rept 35
    in      al,dx
endm
	dec dx
	loop @@regLoop

; =======================
	
	; Init OPL2LPT
	;mov cx, 0F5h
	; Set all registers to 0
;@@regLoop:
	;WriteOPL2LPTAddr LPT_BASE, cl
	;WriteOPL2LPTData LPT_BASE, 0
	;loop @@regLoop
	
; ========================

	; Init DBS2P
	; Enable parallel mode
	;WriteDBS2PCtrl LPT_BASE, 3Fh
	
	; Discard reply byte
	;ReadDBS2PData LPT_BASE

	; Install our own handler	
	cli

	InitPIT CHAN0, %(MODE2 or AMBOTH or BINARY), 2
	
	push    ds
	xor     bx, bx
	mov     ds, bx
	mov     bx, ds:[20h]
	mov     word ptr cs:[oldint8], bx
	mov     bx, ds:[22h]
	mov     word ptr cs:[oldint8+2], bx
	
	mov     word ptr ds:[20h], 32768
	mov     word ptr ds:[22h], es
	
	mov     bx, ds:[24h]
	mov     word ptr cs:[oldint9], bx
	mov     bx, ds:[26h]
	mov     word ptr cs:[oldint9+2], bx

	mov     word ptr ds:[24h], offset KeyHandler
	mov     word ptr ds:[26h], cs
	
	pop     ds

	; Now initialize the counter with the first count
	seges lodsb
	out CHAN0PORT, al
	seges lodsb
	out CHAN0PORT, al

	sti
	
mainloop:
	; Check if we need to buffer anything
sampleBufCheck:
	mov ax, word ptr es:[sampleBufIns+1+32768]
	
	; Do we also need to relocate our handler?
	cmp [timerHandlerHi], 0
	jne checkTimerHi
	
checkTimerLo:
	cmp ax, 32768 + (EndTimerHandler-TimerHandler+1)
	ja doHandler
	jmp waitKey

doHandler:	
	; Copy handler to sample buffer hi position
	add byte ptr ds:[sampleBufInc+4], 32768 shr 8
	mov cx, (EndTimerHandler-TimerHandler+1)/2
	mov si, offset TimerHandler
	mov di, 32768
	rep movsw
	
	; Adjust sample pointer
	hlt
	;cli
	mov si, word ptr es:[sampleBufIns+1]
	mov word ptr es:[sampleBufIns+1+32768], si
	;sti
	
	push ds
	xor si, si
	mov ds, si
	mov byte ptr ds:[20h+1], 32768 shr 8
	pop ds

	mov [timerHandlerHi], 1
	mov word ptr cs:[sampleBufCheck+2], offset sampleBufIns+1+32768
	
IFDEF DEBUG
	; Output character to indicate handler moved to hi
	push ax
	mov ah, 02h
	mov dl, '+'
	int 21h
	pop ax
ENDIF

	call LoadBuffer

	jmp waitKey
	
checkTimerHi:
	cmp ax, 32768
	jae waitKey
	cmp ax, (EndTimerHandler-TimerHandler+1)
	jbe waitKey
	
	; Copy handler to sample buffer lo position
	add byte ptr ds:[sampleBufInc+4], 32768 shr 8
	mov cx, (EndTimerHandler-TimerHandler+1)/2
	mov si, offset TimerHandler
	xor di, di
	rep movsw
	
	; Adjust sample pointer
	hlt
	;cli
	mov si, word ptr es:[sampleBufIns+1+32768]
	mov word ptr es:[sampleBufIns+1], si
	;sti
	
	push ds
	xor si, si
	mov ds, si
	mov byte ptr ds:[20h+1], 0 shr 8
	pop ds
	
	mov [timerHandlerHi], 0
	mov word ptr cs:[sampleBufCheck+2], offset sampleBufIns+1
	
IFDEF DEBUG
	; Output character to indicate handler moved to lo
	push ax
	mov ah, 02h
	mov dl, '-'
	int 21h
	pop ax
ENDIF

	call LoadBuffer
	
waitKey:
IFDEF DEBUG
	; Output character to indicate speed
	mov ah, 02h
	mov dl, '.'
	int 21h
ENDIF
	
	; Wait for keypress or end of file
	cmp cs:[ending], 0
	ja endMainLoop
	jmp mainloop
	
endMainLoop:
	; Wait for player to pass 'bufPos'
	cmp [timerHandlerHi], 0
	jne checkTimerHi2
	
	; Routine is in 'low' position, so sample played in hi buffer
	mov ax, word ptr es:[sampleBufIns+1]
	cmp ax, cs:[bufPos]
	jb waitKey2
	inc cs:[ending]
	jmp waitKey2
	
checkTimerHi2:
	; Routine is in 'hi' position, so sample played in lo buffer
	mov ax, word ptr es:[sampleBufIns+1+32768]
	cmp ax, BUFSIZE
	jae waitKey2
	cmp ax, cs:[bufPos]
	jb waitKey2
	inc cs:[ending]

waitKey2:
	; Wait for keypress
	cmp cs:[ending], 2
	jb endMainLoop

	; Close music stream file
	mov ah, 03Eh
	mov bx, cs:[file]
	int 21h
	
	; Restore old handler
	cli
	push    ds
	xor     bx,bx
	mov     ds,bx
	mov     bx,word ptr cs:[oldint8]
	mov     ds:[20h],bx
	mov     bx,word ptr cs:[oldint8+2]
	mov     ds:[22h],bx

	mov     bx,word ptr cs:[oldint9]
	mov     ds:[24h],bx
	mov     bx,word ptr cs:[oldint9+2]
	mov     ds:[26h],bx

	pop     ds
	
	; Restore timer interrupt speed
	ResetPIT CHAN0
	
	sti

	; Get PIC out of auto-EOI mode
	mov cl, [machineType]
	call RestorePIC
	;call ClosePCSpeaker
	;call ClosePCjrAudio
	
	; Free sample buffer
	mov es, [sampleBufSeg]
	mov ah, 49h
	int 21h
	
err:
	pop es
	
	; Restore interrupts
	;mov al, [picMask]
	;out PIC1_DATA, al
	
	; Exit program
	mov ax, 4C00h
	int 21h
	
; === End program ===

LoadHeader proc
	; Read the header from file
	mov ah, 03Fh
	mov bx, cs:[file]
	mov cx, size preHeader
	mov dx, offset preHeader
	int 21h
	
	; Search to start of data
	mov ah, 042h
	mov al, 00h	; Start of file
	mov bx, cs:[file]
	mov dx, word ptr [preHeader.headerLen]
	mov cx, word ptr [preHeader.headerLen+2]
	int 21h

	ret
LoadHeader endp
	
LoadBuffer proc
	push ds
	mov ds, [sampleBufSeg]

	; Read a block from file
	mov ah, 03Fh
	mov bx, cs:[file]
	mov cx, BUFSIZE
	mov dx, cs:[bufPos]
	int 21h
	jc @@EOF
	cmp ax, BUFSIZE
	je @@notEOF
	
@@EOF:
	inc cs:[ending]
	
@@notEOF:
	add cs:[bufPos], ax
	
	pop ds

	ret
LoadBuffer endp
	
; Put this code in the data segment, not executed directly, but copied into place
.data
assume cs:@data
TimerHandler proc
	push si
	push ax
	
	; Load pointer to stream
sampleBufIns:
	mov si, 0002h	; Start at byte 2, to skip the first delay command
	
	; Get note count
	segcs lodsb
	test al, al
	jz endHandler1
		
	; Play notes
	push cx
	xor cx, cx
	mov cl, al
	
	;push bx
	push dx
	;mov dx, 0388h
	
	;jmp enterLoop
	
;noteLoop:
;rept 35
    ;in      al,dx
;endm
	;dec dx

;enterLoop:
	;segcs lodsb
	;out	dx,al
;rept 6
    ;in      al,dx
;endm

    ;inc     dx
	;segcs lodsb
	;out	dx,al
	
	;loop noteLoop
	
; ======================
	mov dx, 0220h
	
	jmp enterLoop1
	
noteLoop1:
rept 3
    in      al,dx
endm
	dec dx

enterLoop1:
	segcs lodsb
	out	dx,al
rept 3
    in      al,dx
endm

    inc     dx
	segcs lodsb
	out	dx,al
	
	loop noteLoop1
	
	pop dx	
	pop cx
	
	; Get note count
endHandler1:
	segcs lodsb
	test al, al
	jz endHandler
		
	; Play notes
	push cx
	xor cx, cx
	mov cl, al
	
	push dx
	
	mov dx, 0222h
	
	jmp enterLoop2
	
noteLoop2:
rept 3
    in      al,dx
endm
	dec dx

enterLoop2:
	segcs lodsb
	out	dx,al
rept 3
    in      al,dx
endm

    inc     dx
	segcs lodsb
	out	dx,al
	
	loop noteLoop2

; ======================
;noteLoop:
	;segcs lodsb
	;xchg ax, bx
	;WriteOPL2LPTAddr LPT_BASE, bl
	
	;segcs lodsb
	;xchg ax, bx
	;WriteOPL2LPTData LPT_BASE, bl

	;loop noteLoop
; ======================

;noteLoop:
;	segcs lodsb
;	xchg ax, bx
;	WriteDBS2PData LPT_BASE, bl
	
;	loop noteLoop

	pop dx		
;	pop bx
	pop cx
		
endHandler:
	; Get delay value from stream
	segcs lodsb
	out CHAN0PORT, al
	segcs lodsb
	out CHAN0PORT, al

	; Update pointer
sampleBufInc:
	mov word ptr cs:[sampleBufIns+32768+1], si

	pop ax		
	pop si
		
	iret
EndTimerHandler:
TimerHandler endp

.code
KeyHandler proc
	push ax
	
	; Read byte from keyboard
	in al,060h
	mov ah, al
	; Acknowledge keyboard
	in al, 061h
	or al, 080h
	out 061h, al
	and al, 07Fh
	out 061h, al
	cmp ah, 1
	jne exitKey
	
	mov cs:[ending], 2
exitKey:

	pop ax

	iret
KeyHandler endp

InitPCSpeaker proc
	cli

	; Enable speaker and tie input pin to CTC Chan 2 by setting bits 1 and 0
	in al, PPIPORTB
	or al, 03
	out PPIPORTB, al
	
	; Counter 2 count = 1 - terminate count quickly
	InitPIT CHAN2, %(MODE0 or AMLOBYTE or BINARY), 1
	
	sti
	
	ret
InitPCSpeaker endp

ClosePCSpeaker proc
	cli

	; Disable speaker by clearing bits 1 and 0
	in al, PPIPORTB
	and al, not 03
	out PPIPORTB, al
	
	; Reset timer
	ResetPIT CHAN2
	
	sti
	
	ret
ClosePCSpeaker endp

InitPCjrAudio proc
	; Audio Multiplexer is Int1A AH=80 AL=Audio source (0=PC speaker, 1=Cassette, 2=I/O channel "Audio In", 3=SN76496).
	mov al, 3
	int 1Ah
	
	ret
InitPCjrAudio endp

ClosePCjrAudio proc
	; Silence all SN76489 channels
	mov dx, 0C0h
	mov al, 0FFh
	out dx, al
	mov al, 0BFh
	out dx, al
	mov al, 0DFh
	out dx, al
	mov al, 09Fh
	out dx, al
	
	; Reset the multiplexor
	; Audio Multiplexer is Int1A AH=80 AL=Audio source (0=PC speaker, 1=Cassette, 2=I/O channel "Audio In", 3=SN76496).
	mov al, 0
	int 1Ah
	
	ret
ClosePCjrAudio endp

.code
ending	db	0
timerHandlerHi	db	1
bufPos	dw	0

oldint8	dd	?
oldint9	dd	?
file dw ?

.data
usageSt db "Usage: PlayVGM <filename.ext>",0dh,0ah,'$'
errorFile db "Unable to open file!",0dh,0ah,'$'

.data?  
picMask		db	?
machineType	db	?
sampleBufSeg	dw	?
preHeader PreHeader <>
_cmdline		CMDLINE	<>
fileName  db 126 DUP (?)

END start
