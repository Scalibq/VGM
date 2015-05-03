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
playLoop:
	call	DoTick

	mov		al, cs:[playing]
	test	al, al
	jnz		playLoop
	
openFailed:
	; Exit process
	mov ax,	4C00h
	int		21h

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