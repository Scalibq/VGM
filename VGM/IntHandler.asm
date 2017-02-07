.8086

public Handler_, pBuf, _SetBuf, _GetBuf

extern _pBuf:PTR DWORD

CHAN0PORT equ 040h

IntHandler segment para public 'IntHandler'

_SetBuf PROC
	retn

	push ds
	push si
	
	mov ax, seg _pBuf
	mov ds, ax
	lds si, [_pBuf]
	mov word ptr cs:[pBuf], si
	mov word ptr cs:[pBuf+2], ds
	
	pop si
	pop ds
	
	retn
_SetBuf ENDP

_GetBuf PROC
	retn

	push ds
	push es
	push si
	
	mov ax, seg _pBuf
	mov es, ax
	lds si, cs:[pBuf]
	mov word ptr es:[_pBuf], si
	mov word ptr es:[_pBuf+2], ds
	
	pop si
	pop es
	pop ds
	
	retn
_GetBuf ENDP

Handler_ PROC
		push ds
		push si
		push ax
		
		mov ax, seg _pBuf
		mov ds, ax
		lds si, [_pBuf]
		
		; Get note count
		lodsb
		test al, al
		jz endHandler
		
		; Play notes
		push cx
		xor cx, cx
		mov cl, al
		
	noteLoop:
		lodsb
		out 0C0h, al
		
		loop noteLoop
		
		pop cx
		
	endHandler:
		; Get delay value from stream
		lodsb
		out CHAN0PORT, al
		lodsb
		out CHAN0PORT, al

		mov ax, seg _pBuf
		mov ds, ax
		mov word ptr [_pBuf], si
		
		pop ax		
		pop si
		pop ds
		
		iret
Handler_ ENDP

pBuf	dd ?

IntHandler ends

END