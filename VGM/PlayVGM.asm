.8086
.Model Small

VFileIdent equ 0206d6756h
SNReg equ 0C0h
SNFreq equ 3579540
SNMplxr equ 061h	; MC14529b sound multiplexor chip in the PCjr
SampleRate equ 44100
PITfreq equ 1193182

PUBLIC DoTick_
PUBLIC _stopped
PUBLIC _pSong

.code
DoTick_ PROC
	push ds
	lds si, cs:[_pSong]
	
nextCommand:
	lodsb
	cmp al, 050h
	jne noNote
	lodsb
	out SNReg, al
	jmp nextCommand
	
noNote:
	cmp al, 062h
	je doneTick
	cmp al, 063h
	je doneTick
	
	cmp al, 066h
	je endSong
	
	jmp nextCommand
	
endSong:
	mov cs:[_stopped], 1
	
doneTick:
	mov word ptr cs:[_pSong], si
	
	pop ds

	retn
DoTick_ ENDP

_stopped db 0
_pSong dd ?

END