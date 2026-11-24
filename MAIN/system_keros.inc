; KerOS OS-layer for FASM
; Final clean rewrite.

KAPI_PRINTF    equ 4
KAPI_FS_READ   equ 20
KAPI_FS_WRITE  equ 76
KAPI_ALLOC     equ 176
KAPI_FREE      equ 180
KAPI_FS_SIZE   equ 192
KAPI_ARG       equ 68

MAX_OPEN       equ 16
FD_PATH_MAX    equ 256
CMDLINE_MAX    equ 4096
ARGV_MAX       equ 63

; ---------------- данные ----------------

kapi_ptr       dd ?
saved_esp      dd ?

argv_block     rd 66
env_block      dd ?
cmdline_buf    rb CMDLINE_MAX
prog_name      db 'fasm',0

tmp_size       dd ?
tmp_len        dd ?
tmp_cap        dd ?
tmp_ptr        dd ?

; Дескрипторы 1..MAX_OPEN-1. 0 всегда невалиден.
fd_state       rd MAX_OPEN     ; 0 free, 1 read, 2 write
fd_ptr         rd MAX_OPEN
fd_size        rd MAX_OPEN
fd_pos         rd MAX_OPEN
fd_cap         rd MAX_OPEN
fd_path        rb MAX_OPEN * FD_PATH_MAX

fmt_s          db '%s',0
fmt_c          db '%c',0

ker_nomem_msg  db 'FASM: alloc failed',0Ah,0
error_prefix   db 'error: ',0
error_suffix   db '.',0Ah,0

; ---------------- вспомогательные ----------------

ker_alloc:
	; ecx = size, возвращает eax
	push	ecx
	mov	esi,[kapi_ptr]
	push	ecx
	call	dword [esi+KAPI_ALLOC]
	add	esp,4
	pop	ecx
	ret

ker_free:
	; eax = ptr
	push	eax
	mov	esi,[kapi_ptr]
	push	eax
	call	dword [esi+KAPI_FREE]
	add	esp,4
	pop	eax
	ret

ker_zero:
	; eax = ptr, ecx = len
	push	eax
	push	ecx
	push	edi
	cld
	mov	edi,eax
	xor	eax,eax
	rep	stosb
	pop	edi
	pop	ecx
	pop	eax
	ret

ker_strcpy_bounded:
	; esi = src, edi = dst, ecx = limit
	push	eax
	cld
.loop:
	cmp	ecx,0
	je	.end
	lodsb
	mov	[edi],al
	inc	edi
	dec	ecx
	or	al,al
	jnz	.loop
.end:
	mov	byte [edi],0
	pop	eax
	ret

; ---------------- память ----------------

init_memory:
	mov	eax,esp
	and	eax,not 0FFFh
	add	eax,1000h-10000h
	mov	[stack_limit],eax

	push	ebx
	push	esi
	push	edi
	push	ebp

	mov	ecx,[memory_setting]
	shl	ecx,10
	jnz	.user_size
	mov	ecx,1000000h          ; 16 MiB по умолчанию

.user_size:
	mov	ebp,ecx               ; размер в callee-saved регистре

.try_alloc:
	mov	esi,[kapi_ptr]
	push	ebp
	call	dword [esi+KAPI_ALLOC]
	add	esp,4

	test	eax,eax
	jnz	.alloc_ok

	; Фолбэк: 16 -> 8 -> 4 -> 2 -> 1 MiB
	shr	ebp,1
	cmp	ebp,100000h
	jae	.try_alloc

	jmp	.nomem

.alloc_ok:
	mov	ebx,eax               ; указатель в callee-saved регистре

	mov	[additional_memory],ebx

	lea	edx,[ebx+ebp]
	mov	[memory_end],edx

	mov	edx,ebp
	shr	edx,2
	add	edx,ebx
	mov	[additional_memory_end],edx
	mov	[memory_start],edx

	pop	ebp
	pop	edi
	pop	esi
	pop	ebx
	ret

.nomem:
	mov	esi,ker_nomem_msg
	call	display_string
	mov	al,0FFh

	pop	ebp
	pop	edi
	pop	esi
	pop	ebx
	jmp	exit_program

; ---------------- выход / время / окружение ----------------

exit_program:
	mov	esp,[saved_esp]
	ret

get_environment_variable:
	ret

keros_time_ms:
	xor	eax,eax
	ret

make_timestamp:
	xor	eax,eax
	xor	edx,edx
	ret

; ---------------- командная строка ----------------

build_argv:
	push	esi
	push	edi
	push	ebx
	push	ecx
	push	edx

	cld

	mov	edi,argv_block
	mov	dword [edi],1
	mov	dword [edi+4],prog_name
	mov	ecx,1

	mov	esi,[kapi_ptr]
	mov	eax,[esi+KAPI_ARG]
	test	eax,eax
	jz	.done

	mov	esi,eax
	mov	edi,cmdline_buf
	lea	ebx,[cmdline_buf + CMDLINE_MAX - 1]

.copy:
	lodsb
	or	al,al
	jz	.parse
	cmp	edi,ebx
	jae	.parse
	stosb
	jmp	.copy

.parse:
	mov	byte [edi],0

	mov	esi,cmdline_buf
	xor	ebx,ebx

.scan:
	mov	al,[esi]
	or	al,al
	jz	.done

	cmp	al,' '
	je	.sep
	cmp	al,9
	je	.sep
	cmp	al,10
	je	.sep
	cmp	al,13
	je	.sep

	test	ebx,ebx
	jnz	.next

	cmp	ecx,ARGV_MAX
	jae	.next

	mov	[argv_block+4+ecx*4],esi
	inc	ecx
	mov	ebx,1

.next:
	inc	esi
	jmp	.scan

.sep:
	mov	byte [esi],0
	xor	ebx,ebx
	inc	esi
	jmp	.scan

.done:
	mov	[argv_block],ecx
	mov	dword [argv_block+4+ecx*4],0
	mov	dword [env_block],0

	pop	edx
	pop	ecx
	pop	ebx
	pop	edi
	pop	esi
	ret

; ---------------- файловая подсистема ----------------

find_free_slot:
	xor	eax,eax
.next:
	inc	eax
	cmp	eax,MAX_OPEN
	jae	.none
	cmp	dword [fd_state+eax*4],0
	jne	.next
	ret
.none:
	xor	eax,eax
	ret

validate_handle:
	cmp	ebx,1
	jb	.bad
	cmp	ebx,MAX_OPEN
	jae	.bad
	cmp	dword [fd_state+ebx*4],0
	je	.bad
	clc
	ret
.bad:
	stc
	ret

adapt_path:
	push	eax
	push	ecx
	push	esi
	push	edi

	cld

	mov	esi,edx
	mov	edi,buffer
	mov	ecx,1000h - 1

.loop:
	lodsb

	cmp	al,5Ch                ; '\'
	jne	.ok
	mov	al,'/'

.ok:
	mov	[edi],al
	inc	edi
	dec	ecx

	or	al,al
	jz	.done

	cmp	ecx,0
	jne	.loop

.done:
	mov	byte [edi],0

	pop	edi
	pop	esi
	pop	ecx
	pop	eax
	ret

copy_path_to_slot:
	; ebx = slot
	push	eax
	push	ecx
	push	esi
	push	edi

	mov	edi,ebx
	shl	edi,8
	lea	edi,[fd_path+edi]

	mov	esi,buffer
	mov	ecx,FD_PATH_MAX - 1

	call	ker_strcpy_bounded

	pop	edi
	pop	esi
	pop	ecx
	pop	eax
	ret

ensure_write_capacity:
	; ebx = slot, ecx = needed capacity
	cmp	ecx,[fd_cap+ebx*4]
	jbe	.ok

	push	eax
	push	ecx
	push	esi
	push	edi

	mov	edi,ecx

	mov	eax,[fd_cap+ebx*4]
	test	eax,eax
	jnz	.cap_ok
	mov	eax,65536

.cap_ok:
.grow:
	cmp	eax,edi
	jae	.alloc
	shl	eax,1
	jc	.fail
	jmp	.grow

.alloc:
	mov	[tmp_cap],eax

	mov	ecx,eax
	call	ker_alloc
	test	eax,eax
	jz	.fail

	mov	[tmp_ptr],eax

	mov	ecx,[tmp_cap]
	call	ker_zero

	mov	ecx,[fd_size+ebx*4]
	test	ecx,ecx
	jz	.copied

	cld
	mov	esi,[fd_ptr+ebx*4]
	mov	edi,[tmp_ptr]
	rep	movsb

.copied:
	mov	eax,[fd_ptr+ebx*4]
	test	eax,eax
	jz	.old_zero
	call	ker_free

.old_zero:
	mov	eax,[tmp_ptr]
	mov	[fd_ptr+ebx*4],eax

	mov	eax,[tmp_cap]
	mov	[fd_cap+ebx*4],eax

	pop	edi
	pop	esi
	pop	ecx
	pop	eax
.ok:
	clc
	ret

.fail:
	pop	edi
	pop	esi
	pop	ecx
	pop	eax
	stc
	ret

open:
	push	esi
	push	edi
	push	ebp

	call	adapt_path
	call	find_free_slot
	test	eax,eax
	jz	.fail

	mov	ebx,eax

	mov	esi,[kapi_ptr]
	push	tmp_size
	push	buffer
	call	dword [esi+KAPI_FS_SIZE]
	add	esp,8

	test	eax,eax
	js	.fail

	mov	ecx,[tmp_size]
	lea	eax,[ecx+1]
	mov	ecx,eax
	call	ker_alloc
	test	eax,eax
	jz	.fail

	mov	ebp,eax

	mov	esi,[kapi_ptr]
	push	tmp_len
	push	dword [tmp_size]
	push	ebp
	push	buffer
	call	dword [esi+KAPI_FS_READ]
	add	esp,16

	test	eax,eax
	js	.free_fail

	mov	eax,[tmp_len]
	cmp	eax,[tmp_size]
	jne	.free_fail

	mov	ecx,[tmp_size]
	mov	byte [ebp+ecx],0

	mov	dword [fd_state+ebx*4],1
	mov	[fd_ptr+ebx*4],ebp

	mov	eax,[tmp_size]
	mov	[fd_size+ebx*4],eax
	mov	dword [fd_pos+ebx*4],0

	inc	eax
	mov	[fd_cap+ebx*4],eax

	call	copy_path_to_slot

	clc
	pop	ebp
	pop	edi
	pop	esi
	ret

.free_fail:
	mov	eax,ebp
	call	ker_free

.fail:
	stc
	pop	ebp
	pop	edi
	pop	esi
	ret

create:
	push	esi
	push	edi
	push	ebp

	call	adapt_path
	call	find_free_slot
	test	eax,eax
	jz	.fail

	mov	ebx,eax

	mov	ecx,65536
	call	ker_alloc
	test	eax,eax
	jz	.fail

	mov	ebp,eax

	mov	ecx,65536
	call	ker_zero

	mov	dword [fd_state+ebx*4],2
	mov	[fd_ptr+ebx*4],ebp
	mov	dword [fd_size+ebx*4],0
	mov	dword [fd_pos+ebx*4],0
	mov	dword [fd_cap+ebx*4],65536

	call	copy_path_to_slot

	clc
	pop	ebp
	pop	edi
	pop	esi
	ret

.fail:
	stc
	pop	ebp
	pop	edi
	pop	esi
	ret

close:
	push	ebx
	push	esi
	push	edi

	call	validate_handle
	jc	.done

	cmp	dword [fd_state+ebx*4],2
	jne	.free

	mov	esi,[kapi_ptr]

	push	dword [fd_size+ebx*4]
	push	dword [fd_ptr+ebx*4]

	mov	edi,ebx
	shl	edi,8
	lea	edi,[fd_path+edi]
	push	edi

	call	dword [esi+KAPI_FS_WRITE]
	add	esp,12

.free:
	mov	eax,[fd_ptr+ebx*4]
	test	eax,eax
	jz	.clear
	call	ker_free

.clear:
	mov	dword [fd_state+ebx*4],0
	mov	dword [fd_ptr+ebx*4],0
	mov	dword [fd_size+ebx*4],0
	mov	dword [fd_pos+ebx*4],0
	mov	dword [fd_cap+ebx*4],0

.done:
	xor	eax,eax
	pop	edi
	pop	esi
	pop	ebx
	ret

read:
	push	esi
	push	edi
	push	ebp

	call	validate_handle
	jc	.fail

	cmp	dword [fd_state+ebx*4],1
	jne	.fail

	mov	ebp,ecx
	test	ebp,ebp
	jz	.ok

	mov	eax,[fd_size+ebx*4]
	sub	eax,[fd_pos+ebx*4]
	cmp	eax,ebp
	jb	.fail

	cld

	mov	esi,[fd_ptr+ebx*4]
	add	esi,[fd_pos+ebx*4]
	mov	edi,edx
	mov	ecx,ebp

	rep	movsb

	add	[fd_pos+ebx*4],ebp

.ok:
	mov	eax,ebp
	clc
	pop	ebp
	pop	edi
	pop	esi
	ret

.fail:
	stc
	pop	ebp
	pop	edi
	pop	esi
	ret

write:
	push	esi
	push	edi
	push	ebp

	call	validate_handle
	jc	.fail

	cmp	dword [fd_state+ebx*4],2
	jne	.fail

	mov	ebp,ecx
	test	ebp,ebp
	jz	.ok

	mov	eax,[fd_pos+ebx*4]
	add	eax,ebp
	jc	.fail

	mov	ecx,eax
	call	ensure_write_capacity
	jc	.fail

	cld

	mov	esi,edx
	mov	edi,[fd_ptr+ebx*4]
	add	edi,[fd_pos+ebx*4]
	mov	ecx,ebp

	rep	movsb

	mov	eax,[fd_pos+ebx*4]
	add	eax,ebp
	mov	[fd_pos+ebx*4],eax

	cmp	eax,[fd_size+ebx*4]
	jbe	.size_ok
	mov	[fd_size+ebx*4],eax

.size_ok:
.ok:
	mov	eax,ebp
	clc
	pop	ebp
	pop	edi
	pop	esi
	ret

.fail:
	stc
	pop	ebp
	pop	edi
	pop	esi
	ret

lseek:
	call	validate_handle
	jc	.fail

	cmp	al,0
	je	.set
	cmp	al,1
	je	.cur
	cmp	al,2
	je	.end
	jmp	.fail

.set:
	mov	eax,edx
	jmp	.check

.cur:
	mov	eax,[fd_pos+ebx*4]
	add	eax,edx
	jmp	.check

.end:
	mov	eax,[fd_size+ebx*4]
	add	eax,edx

.check:
	jc	.fail

	cmp	dword [fd_state+ebx*4],1
	jne	.write_seek

	cmp	eax,[fd_size+ebx*4]
	ja	.fail

	mov	[fd_pos+ebx*4],eax
	clc
	ret

.write_seek:
	push	ecx
	mov	ecx,eax
	call	ensure_write_capacity
	pop	ecx
	jc	.fail

	mov	[fd_pos+ebx*4],eax

	cmp	eax,[fd_size+ebx*4]
	jbe	.no_extend
	mov	[fd_size+ebx*4],eax

.no_extend:
	clc
	ret

.fail:
	stc
	ret

file_error:
	stc
	ret

; ---------------- вывод ----------------

display_string:
	push	eax
	push	ebx
	push	ecx
	push	edx
	push	esi

	push	esi
	push	fmt_s

	mov	eax,[kapi_ptr]
	call	dword [eax+KAPI_PRINTF]
	add	esp,8

	pop	esi
	pop	edx
	pop	ecx
	pop	ebx
	pop	eax
	ret

display_character:
	push	eax
	push	ebx
	push	ecx
	push	edx
	push	esi

	movzx	eax,dl
	push	eax
	push	fmt_c

	mov	eax,[kapi_ptr]
	call	dword [eax+KAPI_PRINTF]
	add	esp,8

	pop	esi
	pop	edx
	pop	ecx
	pop	ebx
	pop	eax
	ret

display_number:
	push	ebx
	push	ecx
	push	edx

	mov	ecx,1000000000
	xor	edx,edx
	xor	bl,bl

ker_dn_loop:
	div	ecx
	push	edx

	cmp	ecx,1
	je	ker_dn_digit

	or	bl,bl
	jnz	ker_dn_digit

	or	al,al
	jz	ker_dn_ok

	not	bl

ker_dn_digit:
	mov	dl,al
	add	dl,30h

	push	ebx
	push	ecx
	call	display_character
	pop	ecx
	pop	ebx

ker_dn_ok:
	mov	eax,ecx
	xor	edx,edx
	mov	ecx,10
	div	ecx
	mov	ecx,eax

	pop	eax

	or	ecx,ecx
	jnz	ker_dn_loop

	pop	edx
	pop	ecx
	pop	ebx
	ret

display_block:
	jecxz	ker_db_done

	add	[displayed_count],ecx

	mov	al,[esi+ecx-1]
	mov	[last_displayed],al

	push	eax
	push	ebx
	push	ecx
	push	edx
	push	esi

ker_db_loop:
	mov	dl,[esi]
	call	display_character
	inc	esi
	loop	ker_db_loop

	pop	esi
	pop	edx
	pop	ecx
	pop	ebx
	pop	eax

ker_db_done:
	ret

display_user_messages:
	mov	[displayed_count],0
	call	show_display_buffer

	cmp	[displayed_count],0
	je	.ok

	cmp	[last_displayed],0Ah
	je	.ok

	mov	dl,0Ah
	call	display_character

.ok:
	ret

; ---------------- ошибки ----------------

fatal_error:
	mov	esi,error_prefix
	call	display_string

	pop	esi
	call	display_string

	mov	esi,error_suffix
	call	display_string

	mov	al,0FFh
	jmp	exit_program

assembler_error:
	call	display_user_messages

	mov	esi,error_prefix
	call	display_string

	pop	esi
	call	display_string

	mov	esi,error_suffix
	call	display_string

	mov	al,2
	jmp	exit_program