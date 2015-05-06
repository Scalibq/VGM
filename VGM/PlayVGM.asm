.8086
.Model Small
.stack 100h

VFileIdent equ 0206d6756h
SNReg equ 0C0h
SNFreq equ 3579540
SNMplxr equ 061h	; MC14529b sound multiplexor chip in the PCjr
SampleRate equ 44100
PITfreq equ 1193182

.code
start:
	push	cs
	pop		ds

	; Open VGM file
	mov		ax, 3D00h
	mov		dx, offset fileName
	int		21h
	jc		openFailed

continue:
	; Store file handle
	mov		[fileHandle], ax
	
	; Seek to get the file size
	mov		bx, ax
	mov		ax, 4202h
	xor		cx, cx
	xor		dx, dx
	int		21h
	mov		word ptr [fileSize], ax
	mov		word ptr [fileSize+2], dx
	
	xor		dx, dx
	mov		ax, 4200h
	int		21h
	
	mov		ah, 4Ah
	mov		bx, 1000h
	int		21h		; DOS -	2+ - ADJUST MEMORY BLOCK SIZE (SETBLOCK)
					; ES = segment address of block	to change
					; BX = new size	in paragraphs
	
	; Allocate memory
	mov		bx, word ptr [fileSize]
	add		bx, 15	; Round up
	shr		bx, 1
	shr		bx, 1
	shr		bx, 1
	shr		bx, 1
	mov		ah, 48h
	int		21h
	mov		word ptr [pSong+2], ax	
	
	; Load data
	push	ds	
	mov		ah, 3Fh
	mov		bx, [fileHandle]
	mov		cx, word ptr [fileSize]
	lds		dx, [pSong]
	int		21h
	
	pop		ds
	
	; Close file handle
	mov		ah, 3Eh
	mov		bx, fileHandle]
	int		21h
	
	; Play file
	; Adjust file pointer
	add		word ptr cs:[pSong], 40h
	
	; Init PIT at right frequency and mode
	mov al, 34h
	out 43h, al
	
playLoop:
	call	DoTick
	
	call	DoWait

	mov		al, cs:[playing]
	test	al, al
	jnz		playLoop
	
openFailed:
	; Exit process
	mov ax,	4C00h
	int		21h
	
iMC_Chan0	equ 0
iMC_LatchCounter	equ 0
iMC_OpMode2	equ 100b
iMC_BinaryMode	equ 0
numticks equ (PITfreq/60)
	
DoWait PROC
  ; Build PIT command: Channel 0, Latch Counter, Rate Generator, Binary
  mov    bh,iMC_Chan0
  mov    al,bh
  ; get initial count
  cli
  out    43h,al         ; Tell timer about it
  in     al,40h         ; Get LSB of timer counter
  xchg   al,ah          ; Save it in ah (xchg accum,reg is 3c 1b)
  in     al,40h         ; Get MSB of timer counter
  sti
  xchg   al,ah          ; Put things in the right order; AX:=starting timer
  mov    dx,ax          ; store for later

@wait:
  ; get next count
  mov    al,bh          ; Use same Mode/Command as before (latch counter, etc.)
  cli                   ; Disable interrupts so our operation is atomic
  out    43h,al         ; Tell timer about it
  in     al,40h         ; Get LSB of timer counter
  xchg   al,ah          ; Save it in ah for a second
  in     al,40h         ; Get MSB of timer counter
  sti
  xchg   al,ah          ; AX:=new timer value
  ; see if our count period has elapsed
  mov    si,dx          ; copy original value to scratch
  sub    si,ax          ; subtract new value from old value
  cmp    si,numticks    ; compare si to maximum time allowed
  jb     @wait          ; if still below, keep waiting; if above, blow doors
  
  ret
DoWait ENDP

DoTick PROC
	push	ds
	lds		si, cs:[pSong]
	
nextCommand:
	lodsb
	cmp		al, 050h
	jne		noNote
	lodsb
	out		SNReg, al
	jmp		nextCommand
	
noNote:
	cmp		al, 062h
	je		doneTick
	cmp		al, 063h
	je		doneTick
	
	cmp		al, 066h
	je		endSong
	
	jmp		nextCommand
	
endSong:
	mov		byte ptr cs:[playing], 0
	
doneTick:
	mov		word ptr cs:[pSong], si
	
	pop		ds

	ret
DoTick ENDP

filename	db "pcjr3.vgm",0
playing		db 1
pSong		dd 0

fileHandle	dw ?
fileSize	dd ?

END start