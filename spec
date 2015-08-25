#define STACKSIZE	0x3000


beginseg
	name "code"
	flags BOOT OBJECT
	entry boot
	stack bootStack + STACKSIZE
	include "codesegment.o"
	include "$(ROOT)/usr/lib/PR/rspboot.o"
	include "$(ROOT)/usr/lib/PR/gspFast3D.o"
endseg

beginseg
	name "cfb"
	flags OBJECT
	align 64
	after code
	include "cfb.o"
endseg

beginwave
	name "main"
	include "code"
	include "cfb"
endwave
