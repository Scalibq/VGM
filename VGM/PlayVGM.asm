.8086
.Model Small

include common.inc
include 8253.inc
include 8259A.inc

BUFSIZE equ 32768
NUMBUF equ 1

LOCALS @@

.stack 100h

.code
start:
	push es

	; Set DS to .data
	mov ax, @data
	mov ds, ax
	
	; Disable all interrupts except timer and keyboard
	;in al, PIC1_DATA
	;mov [picMask], al
	;mov al, not 03h
	;out PIC1_DATA, al

	mov		ah, 4Ah
	mov		bx, 3000h
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
	
	; Preprocess sample
REPT NUMBUF
	call MixBuffer
ENDM

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
	call InitPCjrAudio

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
	; Check if we need to mix anything
sampleBufCheck:
	mov ax, word ptr es:[sampleBufIns+1+32768]
	
	; Do we also need to relocate our handler?
	cmp [timerHandlerHi], 0
	jne checkTimerHi
	
checkTimerLo:
	cmp ax, 32768 + (EndTimerHandler-TimerHandler+1)
	ja doHandler
	jmp doMixBuffer

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

	call MixBuffer

	jmp doMixBuffer
	
checkTimerHi:
	cmp ax, 32768
	jae doMixBuffer
	cmp ax, (EndTimerHandler-TimerHandler+1)
	jbe doMixBuffer
	
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

	call MixBuffer
	
doMixBuffer:	
	;cmp ax, cs:[mixPos]
	;ja waitKey
	;add ax, (BUFSIZE)
	;cmp ax, cs:[mixPos]
	;jb waitKey

	;call MixBuffer
	;mov ax, word ptr es:[sampleBufIns+1+32768]
	;jmp doMixBuffer

IFDEF DEBUG
	; Output character to indicate speed
	mov ah, 02h
	mov dl, '.'
	int 21h
ENDIF
	
waitKey:
	; Wait for keypress or end of file
	cmp cs:[ending], 0
	ja endMainLoop
	jmp mainloop
	
endMainLoop:
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
	call ClosePCjrAudio
	
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
	
MixBuffer proc
	cmp cs:[ending], 0
	jne @@endMixBuffer

	push ds
	mov ds, [sampleBufSeg]

	; Read a block from file
	mov ah, 03Fh
	mov bx, cs:[file]
	mov cx, BUFSIZE
	mov dx, cs:[mixPos]
	int 21h
	jc @@EOF
	cmp ax, BUFSIZE
	je @@notEOF
	
@@EOF:
	inc cs:[ending]
	
	; Entire buffer was not filled, fill remainder with 0s
	;mov cx, BUFSIZE
	;sub cx, ax
	;mov si, cs:[mixPos]
	;add si, ax
	;mov ax, 0	; No commands
	;rep stosb
	
@@notEOF:
	add cs:[mixPos], BUFSIZE
	
	pop ds

@@endMixBuffer:
	ret
MixBuffer endp
	
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
	jz endHandler
		
	; Play notes
	push cx
	xor cx, cx
	mov cl, al
		
noteLoop:
	segcs lodsb
	out 0C0h, al
		
	loop noteLoop
		
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
mixPos	dw	0

oldint8	dd	?
oldint9	dd	?
file dw ?

.data
fileName db "out.pre",0

.data?  
picMask		db	?
machineType	db	?
sampleBufSeg	dw	?

END start
