//64DD Dumper
//Based on pfs SDK demo

#include	<ultra64.h>
#include	<PR/leo.h>
#include	"thread.h"
#include	"textlib.h"
#include	"ncode.h"
#include  <math.h>
#include  <string.h>

#include "fat32.h"
#include "ci.h"

#define NUM_MESSAGE 	1

#define	CONT_VALID	1
#define	CONT_INVALID	0

#define WAIT_CNT	5

extern OSMesgQueue retraceMessageQ;

extern u16      *bitmap_buf;
extern u16      bitmap_buf1[];
extern u16      bitmap_buf2[];
extern u8	backup_buffer[];

OSMesgQueue  		pifMesgQueue;
OSMesg       		dummyMessage;
OSMesg       		pifMesgBuf[NUM_MESSAGE];

static OSContStatus     contstat[MAXCONTROLLERS];
static OSContPad        contdata[MAXCONTROLLERS];
static int		controller[MAXCONTROLLERS];
OSPfs			pfs0;

//DMA PI
#define DMA_QUEUE_SIZE 2
OSMesgQueue dmaMessageQ;
OSMesg    dmaMessageBuf[DMA_QUEUE_SIZE];
OSIoMesg  dmaIOMessageBuf;
OSIoMesg  dmaIoMesgBuf;

OSPiHandle *pi_handle;
OSPiHandle *pi_ddrom_handle;

#define NUM_LEO_MESGS 8
static OSMesg           LeoMessages[NUM_LEO_MESGS];

static u8	blockData[0x1000];
static u32  IPLoffset;

//FAT
//fat_dirent  de_root;
fat_dirent  de_current;
fat_dirent  de_next;
char        fat_message[50];
short       fail;

int fs_fail()
{
	char wait_message[50];
	sprintf(wait_message, "\n    Error (%i): %s", fail, fat_message);
	draw_puts(wait_message);
	return 0;
}

void fat_start(void)
{
  char filenamestr[20];
  char filenamelogstr[20];
  char console_text[50];
  fat_dirent de_root;
  fat_dirent user_dump;
  float fw = ((float)ciGetRevision())/100.0f;
  u32 device_magic = ciGetMagic();
  u32 is64drive = (ciGetMagic() == 0x55444556);

  if(fw < 2.00 || fw > 20.00 || !is64drive){
    draw_puts("\n    - ERROR WRITING TO MEMORY CARD:\n    64DRIVE with FIRMWARE 2.00 or later is required!");
	while(1);
  }
  
  sprintf(console_text, "\n    - 64drive with firmware %.2f found", fw);
  draw_puts(console_text);
  
  sprintf(filenamestr, "64DD_IPL.n64");

  sprintf(console_text, "\n    - Writing %s to memory card", filenamestr);
  draw_puts(console_text);
  
  // get FAT going
  if(fat_init() != 0){
    fail = 100;
    fs_fail();
    return;
  }
  
  // start in root directory
  fat_root_dirent(&de_root);
  if(fail != 0){
    fs_fail();
    return;
  }
  
  
  if( fat_find_create(filenamestr, &de_root, &user_dump, 0, 1) != 0){
    sprintf(fat_message, "Failed to create image dump"); fail = 3;
    fs_fail(); return;
  }

  if( fat_set_size(&user_dump, 0x400000) != 0) {
    sprintf(fat_message, "Failed to resize dump"); fail = 4;
    fs_fail(); return;
  }

  loadRamToRom(0x0, user_dump.start_cluster);
}

void	
mainproc(void *arg)
{	
  int	i, j;
  int	cont_no = 0;
  int	error, readerror;
  u8	bitpattern;
  u16	button = 0, oldbutton = 0, newbutton = 0; 
  u32	stat;
  char 	console_text[50];

  osCreateMesgQueue(&pifMesgQueue, pifMesgBuf, NUM_MESSAGE);
  osSetEventMesg(OS_EVENT_SI, &pifMesgQueue, dummyMessage);

  osContInit(&pifMesgQueue, &bitpattern, &contstat[0]);
  for (i = 0; i < MAXCONTROLLERS; i++) {
    if ((bitpattern & (1<<i)) &&
       ((contstat[i].type & CONT_TYPE_MASK) == CONT_TYPE_NORMAL)) {
      controller[i] = CONT_VALID;
    } else {
      controller[i] = CONT_INVALID;
    }
  }
  
  osCreateMesgQueue(&dmaMessageQ, dmaMessageBuf, 1);

  pi_handle = osCartRomInit();
  pi_ddrom_handle = osDriveRomInit();
  
  bzero(blockData, 0x1000);
  readerror = -1;

  init_draw();

  setcolor(0,255,0);
  draw_puts("If you see this for a long period of time,\nsomething fucked up. Sorry.");
  
  LeoCJCreateLeoManager((OSPri)OS_PRIORITY_LEOMGR-1, (OSPri)OS_PRIORITY_LEOMGR, LeoMessages, NUM_LEO_MESGS);
  LeoResetClear();

  setbgcolor(15,15,15);
  clear_draw();
  
  setcolor(0,255,0);
  draw_puts("\f\n    64DD IPL dumper v0.01b by LuigiBlood & marshallh\n    ----------------------------------------\n");
  setcolor(255,255,255);
  draw_puts("    PRESS START TO DUMP");

  while(1) {
    osContStartReadData(&pifMesgQueue);
    osRecvMesg(&pifMesgQueue, NULL, OS_MESG_BLOCK);
    osContGetReadData(&contdata[0]);
    if (contdata[cont_no].errno & CONT_NO_RESPONSE_ERROR) {
      button = oldbutton;
    } else {
      oldbutton = button;
      button = contdata[cont_no].button;
    }
    newbutton = ~oldbutton & button;
    
    if (newbutton & START_BUTTON)
    {
        //DUMP!!
        
        //64drive, enable write to SDRAM/ROM
		  srand(osGetCount()); // necessary to generate unique short 8.3 filenames on memory card
        ciEnableRomWrites();

        draw_puts("\f\n\n\n\n\n    Let's dump! Don't turn off the console!\n\n");
        
        osInvalDCache((void *)&blockData, (s32) 0x1000); 
        dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_NORMAL;
        dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
        dmaIoMesgBuf.dramAddr = &blockData;
        dmaIoMesgBuf.devAddr = IPLoffset;
        dmaIoMesgBuf.size = 0x1000;
        
        for (IPLoffset = 0; IPLoffset < 0x400000; IPLoffset += 0x1000)
        {
          //read 64DD IPL
          osWritebackDCacheAll();
 
          dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_NORMAL;
          dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
          dmaIoMesgBuf.dramAddr = (void *)&blockData;
          dmaIoMesgBuf.devAddr = 0xA6000000 + IPLoffset;
          dmaIoMesgBuf.size = 0x1000;
                               
          osEPiStartDma(pi_ddrom_handle, &dmaIoMesgBuf, OS_READ);
          osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);
         
          //Write to 64drive
          osWritebackDCacheAll();
 
          dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_NORMAL;
          dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
          dmaIoMesgBuf.dramAddr = (void *)&blockData;
          dmaIoMesgBuf.devAddr = 0xB0000000 + IPLoffset;
          dmaIoMesgBuf.size = 0x1000;
                               
          osEPiStartDma(pi_handle, &dmaIoMesgBuf, OS_WRITE);
          osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);
        }
		  
		  //DONE!! NOW WRITE TO SD
		  fat_start();
        
        draw_puts("\n    - DONE !!\n");
		
        for(;;);
    }
  }
}
