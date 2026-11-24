; �������� � kapi (������ ������, ��������� �� _Static_assert � ����)
KAPI_PRINTF       equ 4
KAPI_FS_READ      equ 20
KAPI_FS_WRITE     equ 76    ; fs_write_file � �������� �� ����������� ���������!
KAPI_FS_FILESIZE  equ 192    ; fs_filesize � ��������!
KAPI_ALLOC        equ 176    ; alloc � ��������!
KAPI_FREE         equ 180    ; free � ��������!

; ��� ������� ������� �� ������� ����� � kapi_t.
; ����� ������� ������� � �������� �������� � ����� kapi.h!
; ��� ��������� ����� ��������� �� ����� _Static_assert � ����
; � �������� ���� ��� -d:

; ���� ��������� ����� build_user.py, �������� ��������:
;   -d KAPI_FS_READ=<off> -d KAPI_FS_WRITE=<off> � �.�.
; ��� ������������ ����� ��������.

; �������� �������
MAX_OPEN_FILES = 16
FILE_PATH_LEN    = 256