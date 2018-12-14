/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 * 
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *   
 * This source code provides page-level FTL scheme. 
 * 
 * Acknowledgement: We thank Jeong Uk Kang by sharing the initial version 
 * of sector-level FTL source code. 
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "flash.h"
#include "ssd_interface.h"

_u32 nand_blk_num,nand_SLC_blk_num,nand_MLC_blk_num, min_fb_num;//用在opm_gc_cost_benefit()
_u8  pb_size;
struct nand_blk_info * nand_blk;
struct SLC_nand_blk_info * SLC_nand_blk;
struct MLC_nand_blk_info * MLC_nand_blk;
struct SLC_nand_blk_info * SLC_head;
struct SLC_nand_blk_info * SLC_tail;

struct SLC_nand_blk_info * SLC_cold_head;
struct SLC_nand_blk_info * SLC_cold_tail;
static int * MLC_nand_blk_bit_map;
double Comb_SLC_Hot_ratio = 0.75;
int merge_switch_num;
int merge_partial_num;
int merge_full_num;

int MLC_last_blk_pc;
static int Min_N_Prime,Liner_S,Liner_L;
int MLC_all_nand_ecn_counts;

double MLC_global_nand_blk_wear_ave;
double MLC_global_nand_blk_wear_std;
double MLC_global_nand_blk_wear_var;
double MLC_global_no_free_nand_blk_wear_ave;
int MLC_min_nand_wear_ave = 1;

int * MLC_nand_pbn_2_lpn_in_CMT_arr;
int * MLC_nand_ppn_2_lpn_in_CMT_arr;
double MLC_wear_level_threshold;
int Wear_Threshold_Type = STATIC_THRESHOLD;
int Wear_Session_Cycle = 1000;
int static_wear_threshold = 20;
double dynamic_wear_beta = 0.1;
int MLC_last_called_wear_num;
int MLC_called_wear_num;
int N_wear_var = 3;
int MIN_ERASE;

/****************small inner function********************/
/*
* add zhoujie 11-8
*/
int isPrime(int n){
	int aqr=0,i;
	if(n <= 1) return 0;
	aqr = (int)sqrt(1.0*n);
	for(i = 2; i <= aqr; i++)
	{
		if (n % i == 0) return 0;
	}
	return 1;
}
/*
* add zhoujie 11-9
*/
int FindMinPrime(int n){
	int res=n;
	while(1){
		if(isPrime(res))
			break;
		res++;
	}
	return res;
}

void MLC_nand_blk_ecn_ave_static()
{
	int i;
	_u32 all_ecn=0;
	for(i = 0;i < nand_MLC_blk_num ; i++) {
		all_ecn += MLC_nand_blk[i].state.ec;
		if(all_ecn >= 4294967296){
			printf("all ecn sum is over limit 4294967296\n");
			assert(0);
		}
	}
	MLC_all_nand_ecn_counts = all_ecn;
	MLC_global_nand_blk_wear_ave = all_ecn* 1.0 /nand_MLC_blk_num;
}

void MLC_nand_no_free_blk_ecn_ave_static()
{
	int i,j=0;
	_u32 all_ecn=0;
	for(i=0 ; i <nand_MLC_blk_num;i++) {
		if(MLC_nand_blk[i].state.free == 0 ){
			all_ecn += MLC_nand_blk[i].state.ec;
			j++;
			if(all_ecn >= 4294967296){
				printf("all ecn sum is over limit 4294967296\n");
				assert(0);
			}
		}
	}
	MLC_global_no_free_nand_blk_wear_ave = all_ecn*1.0/j;
}

void MLC_nand_blk_ecn_std_var_static()
{
  int i;
  double temp = 0.0;
  MLC_nand_blk_ecn_ave_static();
  for(i = 0 ; i < nand_MLC_blk_num ; i++){
	temp += (MLC_nand_blk[i].state.ec - MLC_global_nand_blk_wear_ave) * \
		(MLC_nand_blk[i].state.ec - MLC_global_nand_blk_wear_ave);
  }
  MLC_global_nand_blk_wear_std = temp / nand_MLC_blk_num;
  MLC_global_nand_blk_wear_var = sqrt(MLC_global_nand_blk_wear_std);
}

/*
* add zhoujie 11-10
* static pbn MLC ppn to lpn entry in CMT count
*/
void static_MLC_pbn_map_entry_in_CMT()
{
	int i,j,k;
	int blk_s,blk_e;
	for( i=0 ; i < nand_MLC_blk_num; i++){
		blk_s = M_PAGE_NUM_PER_BLK * i;
		blk_e = M_PAGE_NUM_PER_BLK * (i+1);
		k=0;
		for( j = blk_s ; j < blk_e ; j++){
			if ( MLC_nand_ppn_2_lpn_in_CMT_arr[j] == 1 ){
				k++;	
			}
		}
		MLC_nand_pbn_2_lpn_in_CMT_arr[i] = k;
		MLC_nand_blk_bit_map[i] = k;
	}
}


/**************** NAND STAT **********************/
void nand_stat(int option)
{ 
    switch(option){

      case PAGE_READ:
        stat_read_num++;
        flash_read_num++;
        break;

      case PAGE_WRITE :
        stat_write_num++;
        flash_write_num++;
        break;

      case OOB_READ:
        stat_oob_read_num++;
        flash_oob_read_num++;
        break;

      case OOB_WRITE:
        stat_oob_write_num++;
        flash_oob_write_num++;
        break;

      case BLOCK_ERASE:
        stat_erase_num++;
        flash_erase_num++;
        break;

      case GC_PAGE_READ:
        stat_gc_read_num++;
        flash_gc_read_num++;
        break;
    
      case GC_PAGE_WRITE:
        stat_gc_write_num++;
        flash_gc_write_num++;
        break;
      case SLC_PAGE_READ:
        SLC_stat_read_num++;
        SLC_flash_read_num++;
        break;

      case SLC_PAGE_WRITE :
        SLC_stat_write_num++;
        SLC_flash_write_num++;
        break;

      case SLC_OOB_READ:
        SLC_stat_oob_read_num++;
        SLC_flash_oob_read_num++;
        break;

      case SLC_OOB_WRITE:
        SLC_stat_oob_write_num++;
        SLC_flash_oob_write_num++;
        break;

      case SLC_BLOCK_ERASE:
        SLC_stat_erase_num++;
        SLC_flash_erase_num++;
        break;

      case SLC_GC_PAGE_READ:
        SLC_stat_gc_read_num++;
        SLC_flash_gc_read_num++;
        break;
    
      case SLC_GC_PAGE_WRITE:
        SLC_stat_gc_write_num++;
        SLC_flash_gc_write_num++;
        break;
      case MLC_PAGE_READ:
        MLC_stat_read_num++;
        MLC_flash_read_num++;
        break;

      case MLC_PAGE_WRITE :
        MLC_stat_write_num++;
        MLC_flash_write_num++;
        break;

      case MLC_OOB_READ:
        MLC_stat_oob_read_num++;
        MLC_flash_oob_read_num++;
        break;

      case MLC_OOB_WRITE:
        MLC_stat_oob_write_num++;
        MLC_flash_oob_write_num++;
        break;

      case MLC_BLOCK_ERASE:
        MLC_stat_erase_num++;
        MLC_flash_erase_num++;
        break;

      case MLC_GC_PAGE_READ:
        MLC_stat_gc_read_num++;
        MLC_flash_gc_read_num++;
        break;
    
      case MLC_GC_PAGE_WRITE:
        MLC_stat_gc_write_num++;
        MLC_flash_gc_write_num++;
        break;

      default: 
        ASSERT(0);
        break;
    }
}

void nand_stat_reset()
{
  stat_read_num = stat_write_num = stat_erase_num = 0;
  stat_gc_read_num = stat_gc_write_num = 0;
  stat_oob_read_num = stat_oob_write_num = 0;
  merge_switch_num = merge_partial_num = merge_full_num = 0;
}

void nand_stat_print(FILE *outFP)
{
  int i;
  fprintf(outFP, "\n");
  fprintf(outFP, "FLASH STATISTICS\n");
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, "FLASH Volume is %lf GB\n",(nand_blk_num * 128.0)/(1024*1024));
  fprintf(outFP, " Page read (#): %8u   ", stat_read_num);
  fprintf(outFP, " Page write (#): %8u   ", stat_write_num);
  fprintf(outFP, " Block erase (#): %8u\n", stat_erase_num);
  
  fprintf(outFP, " GC page read (#): %8u   ", stat_gc_read_num);
  fprintf(outFP, " GC page write (#): %8u\n", stat_gc_write_num);
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP,"**************NAND ECN VALUE STATIC*****************************\n");
  for(i=0; i < nand_blk_num ; i++){
	fprintf(outFP,"NANDBLK ECN %d\n",nand_blk[i].state.ec);
  }
}

/**************** NAND INIT **********************/
int nand_init (_u32 blk_num, _u8 min_free_blk_num)
{
  _u32 blk_no;
  int i;

  nand_end();

  nand_blk = (struct nand_blk_info *)malloc(sizeof (struct nand_blk_info) * blk_num);

  if (nand_blk == NULL) 
  {
    return -1;
  }
  memset(nand_blk, 0xFF, sizeof (struct nand_blk_info) * blk_num);

  
  nand_blk_num = blk_num;

  pb_size = 1;
  min_fb_num = min_free_blk_num;
  for (blk_no = 0; blk_no < blk_num; blk_no++) {
    nand_blk[blk_no].state.free = 1;
    nand_blk[blk_no].state.ec = 0;
    nand_blk[blk_no].fpc = SECT_NUM_PER_BLK;
    nand_blk[blk_no].ipc = 0;
    nand_blk[blk_no].lwn = -1;


    for(i = 0; i<SECT_NUM_PER_BLK; i++){
      nand_blk[blk_no].sect[i].free = 1;
      nand_blk[blk_no].sect[i].valid = 0;
      nand_blk[blk_no].sect[i].lsn = -1;
    }

    for(i = 0; i < PAGE_NUM_PER_BLK; i++){
      nand_blk[blk_no].page_status[i] = -1; // 0: data, 1: map table
    }
  }
  free_blk_num = nand_blk_num;

  free_blk_idx =0;

  nand_stat_reset();
  
  return 0;
}

/**************** NAND END **********************/
void nand_end ()
{
  nand_blk_num = 0;
  if (nand_blk != NULL) {
    nand_blk = NULL;
  }
}

/**************** NAND OOB READ **********************/
int nand_oob_read(_u32 psn)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number	
  _u16  pin = IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i, valid_flag = 0;

  ASSERT(pbn < nand_blk_num);	// pbn shouldn't exceed max nand block number 

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
    if(nand_blk[pbn].sect[pin + i].free == 0){

      if(nand_blk[pbn].sect[pin + i].valid == 1){
        valid_flag = 1;//非空闲且有效
        break;
      }
      else{
        valid_flag = -1;//非空闲且无效
        break;
      }
    }
    else{
      valid_flag = 0;//空闲且有效
      break;
    }
  }

  nand_stat(OOB_READ);
  
  return valid_flag;
}


void break_point()
{
  printf("break point\n");
}

/**************** NAND PAGE READ **********************/
_u8 nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number	
  _u16  pin = IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0;

  if(pbn >= nand_blk_num){
    printf("psn: %d, pbn: %d, nand_blk_num: %d\n", psn, pbn, nand_blk_num);
  }

  ASSERT(OFF_F_SECT(psn) == 0);
  if(nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < 8448 ; i++){
      for(j =0; j < 256;j++){
        if(nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(nand_blk[pbn].state.free == 0);	// block should be written with something

  if (isGC == 1) {
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {

      if((nand_blk[pbn].sect[pin + i].free == 0) &&
         (nand_blk[pbn].sect[pin + i].valid == 1)) {
        lsns[valid_sect_num] = nand_blk[pbn].sect[pin + i].lsn;
        valid_sect_num++;
      }
    }

    if(valid_sect_num == 3){
      for(i = 0; i<SECT_NUM_PER_PAGE; i++){
        printf("pbn: %d, pin %d: %d, free: %d, valid: %d\n", 
            pbn, i, pin+i, nand_blk[pbn].sect[pin+i].free, nand_blk[pbn].sect[pin+i].valid);

      }
      exit(0);
    }

  } else if (isGC == 2) {
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {
        if (nand_blk[pbn].sect[pin + i].free == 0 &&
            nand_blk[pbn].sect[pin + i].valid == 1) {
          ASSERT(nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
          valid_sect_num++;
        } else {
          lsns[i] = -1;
        }
      }
    }
  } 

  else { // every sector should be "valid", "not free"   
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {

        ASSERT(nand_blk[pbn].sect[pin + i].free == 0);
        ASSERT(nand_blk[pbn].sect[pin + i].valid == 1);
        ASSERT(nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
        valid_sect_num++;
      }
      else{
        printf("lsns[%d]: %d shouldn't be -1\n", i, lsns[i]);
        exit(0);
      }
    }
  }
  
  if (isGC) {
    if (valid_sect_num > 0) {
      nand_stat(GC_PAGE_READ);
    }
  } else {
    nand_stat(PAGE_READ);
  }
  
  return valid_sect_num;
}

/**************** NAND PAGE WRITE **********************/

_u8 nand_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, valid_sect_num = 0;


  if(pbn >= nand_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_blk_num);
  ASSERT(OFF_F_SECT(psn) == 0);

  if(map_flag == 2) {
        nand_blk[pbn].page_status[pin/SECT_NUM_PER_PAGE] = 1; // 1 for map table
  }
  else{
    nand_blk[pbn].page_status[pin/SECT_NUM_PER_PAGE] = 0; // 0 for data 
  }

  for (i = 0; i <SECT_NUM_PER_PAGE; i++) {

    if (lsns[i] != -1) {

      if(nand_blk[pbn].state.free == 1) {
        printf("blk num = %d",pbn);
      }

      ASSERT(nand_blk[pbn].sect[pin + i].free == 1);
      
      nand_blk[pbn].sect[pin + i].free = 0;			
      nand_blk[pbn].sect[pin + i].valid = 1;			
      nand_blk[pbn].sect[pin + i].lsn = lsns[i];	
      nand_blk[pbn].fpc--;  
      nand_blk[pbn].lwn = pin + i;	
      valid_sect_num++;
    }
    else{
      printf("lsns[%d] do not have any lsn\n", i);
    }
  }
  
  ASSERT(nand_blk[pbn].fpc >= 0);

  if (isGC) {
    nand_stat(GC_PAGE_WRITE);
  } else {
    nand_stat(PAGE_WRITE);
  }

  return valid_sect_num;
}

/**************** NAND BLOCK ERASE **********************/
void nand_erase (_u32 blk_no)
{
  int i;

  ASSERT(blk_no < nand_blk_num);

  ASSERT(nand_blk[blk_no].fpc <= SECT_NUM_PER_BLK);

  if(nand_blk[blk_no].state.free != 0){ printf("debug\n"); }

  ASSERT(nand_blk[blk_no].state.free == 0);

  nand_blk[blk_no].state.free = 1;
  nand_blk[blk_no].state.ec++;
  nand_blk[blk_no].fpc = SECT_NUM_PER_BLK;
  nand_blk[blk_no].ipc = 0;
  nand_blk[blk_no].lwn = -1;


  for(i = 0; i<SECT_NUM_PER_BLK; i++){
    nand_blk[blk_no].sect[i].free = 1;
    nand_blk[blk_no].sect[i].valid = 0;
    nand_blk[blk_no].sect[i].lsn = -1;
  }

  //initialize/reset page status 
  for(i = 0; i < PAGE_NUM_PER_BLK; i++){
    nand_blk[blk_no].page_status[i] = -1;
  }

  free_blk_num++;

  nand_stat(BLOCK_ERASE);
}


/**************** NAND INVALIDATE **********************/
void nand_invalidate (_u32 psn, _u32 lsn)
{
  _u32 pbn = BLK_F_SECT(psn);
  _u16 pin = IND_F_SECT(psn);
  if(pbn > nand_blk_num ) return;

  ASSERT(pbn < nand_blk_num);
  ASSERT(nand_blk[pbn].sect[pin].free == 0);
  if(nand_blk[pbn].sect[pin].valid != 1) { printf("debug"); }
  ASSERT(nand_blk[pbn].sect[pin].valid == 1);

  if(nand_blk[pbn].sect[pin].lsn != lsn){
    ASSERT(0);
  }

  ASSERT(nand_blk[pbn].sect[pin].lsn == lsn);
  
  nand_blk[pbn].sect[pin].valid = 0;
  nand_blk[pbn].ipc++;

  ASSERT(nand_blk[pbn].ipc <= SECT_NUM_PER_BLK);

}

_u32 nand_get_free_blk (int isGC) 
{
  _u32 blk_no = -1, i;
  int flag = 0,flag1=0;
  flag = 0;
  flag1 = 0;

  MIN_ERASE = 9999999;
  //in case that there is no avaible free block -> GC should be called !
  if ((isGC == 0) && (min_fb_num >= free_blk_num)) {
    //printf("min_fb_num: %d\n", min_fb_num);
    return -1;
  }

  for(i = 0; i < nand_blk_num; i++) 
  {
    if (nand_blk[i].state.free == 1) {
      flag1 = 1;

      if ( nand_blk[i].state.ec < MIN_ERASE ) {
            blk_no = i;
            MIN_ERASE = nand_blk[i].state.ec;
            flag = 1;
      }
    }
  }
  if(flag1 != 1){
    printf("no free block left=%d",free_blk_num);
    
  ASSERT(0);
  }
  if ( flag == 1) {
        flag = 0;
        ASSERT(nand_blk[blk_no].fpc == SECT_NUM_PER_BLK);
        ASSERT(nand_blk[blk_no].ipc == 0);
        ASSERT(nand_blk[blk_no].lwn == -1);
        nand_blk[blk_no].state.free = 0;

        free_blk_idx = blk_no;
        free_SLC_blk_num--;

        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }

  return -1;
}

// blk_no=cold_head-&SLC_nand_blk[0];

/*****************************MixSSD Function***************************/
void mix_nand_stat_reset()
{
  SLC_stat_read_num = SLC_stat_write_num = SLC_stat_erase_num = 0;
  SLC_stat_gc_read_num = SLC_stat_gc_write_num = 0;
  SLC_stat_oob_read_num = SLC_stat_oob_write_num = 0;
  MLC_stat_read_num = MLC_stat_write_num = MLC_stat_erase_num = 0;
  MLC_stat_gc_read_num = MLC_stat_gc_write_num = 0;
  MLC_stat_oob_read_num = MLC_stat_oob_write_num = 0;
  //add zhoujie
  translate_map_write_num = 0;
  real_data_write_sect_num = 0;
  page_align_padding_sect_num = 0;
  data_blk_gc_trigger_map_write_num = 0;
  map_blk_gc_trigger_map_write_num = 0; 
  SLC_to_MLC_counts = 0;
  SLC_to_SLC_counts = 0;
  merge_switch_num = merge_partial_num = merge_full_num = 0;
  MLC_last_called_wear_num = MLC_called_wear_num = 0;
}

void mix_nand_end ()
{
  if (SLC_nand_blk != NULL) {
	free(SLC_nand_blk);
    SLC_nand_blk = NULL;
  }
  if (MLC_nand_blk != NULL) {
	free(MLC_nand_blk);
    MLC_nand_blk = NULL;
  }
  if(MLC_nand_blk_bit_map != NULL){
	free(MLC_nand_blk_bit_map);
	MLC_nand_blk_bit_map = NULL;
  }
}

int mix_nand_init (_u32 SLC_blk_num,_u32 MLC_blk_num, _u8 min_free_blk_num)
{
  _u32 blk_no;
  int i;

  mix_nand_end();

  SLC_nand_blk = (struct SLC_nand_blk_info *)malloc(sizeof (struct SLC_nand_blk_info) * SLC_blk_num);
  MLC_nand_blk = (struct MLC_nand_blk_info *)malloc(sizeof (struct MLC_nand_blk_info) * MLC_blk_num);
  if ((SLC_nand_blk == NULL)||(MLC_nand_blk == NULL)) 
  {
    return -1;
  }
  SLC_head = SLC_tail =& SLC_nand_blk[0];
  memset(SLC_nand_blk, 0xFF, sizeof (struct SLC_nand_blk_info) * SLC_blk_num);
  memset(MLC_nand_blk, 0xFF, sizeof (struct MLC_nand_blk_info) * MLC_blk_num);
  
  MLC_nand_blk_bit_map = (int *)malloc(sizeof (int) * MLC_blk_num);
  memset(MLC_nand_blk_bit_map ,0 ,sizeof(int) * MLC_blk_num);
  nand_SLC_blk_num = SLC_blk_num;
  nand_MLC_blk_num = MLC_blk_num;
  pb_size = 1;
  min_fb_num = min_free_blk_num;
  for (blk_no = 0; blk_no < nand_SLC_blk_num; blk_no++) {
    SLC_nand_blk[blk_no].state.free = 1;
    SLC_nand_blk[blk_no].state.ec = 0;
    SLC_nand_blk[blk_no].fpc = S_SECT_NUM_PER_BLK;
    SLC_nand_blk[blk_no].ipc = 0;
    SLC_nand_blk[blk_no].lwn = -1;

    for(i = 0; i<S_SECT_NUM_PER_BLK; i++){
      SLC_nand_blk[blk_no].sect[i].free = 1;
      SLC_nand_blk[blk_no].sect[i].valid = 0;
      SLC_nand_blk[blk_no].sect[i].lsn = -1;
    }

    for(i = 0; i < S_PAGE_NUM_PER_BLK; i++){
      SLC_nand_blk[blk_no].page_status[i] = -1; // 0: data, 1: map table
    }
  }
  for (blk_no =0 ; blk_no < nand_MLC_blk_num; blk_no++) {
    MLC_nand_blk[blk_no].state.free = 1;
    MLC_nand_blk[blk_no].state.ec = 0;
    MLC_nand_blk[blk_no].fpc = M_SECT_NUM_PER_BLK;
    MLC_nand_blk[blk_no].ipc = 0;
    MLC_nand_blk[blk_no].lwn = -1;

    for(i = 0; i<M_SECT_NUM_PER_BLK; i++){
      MLC_nand_blk[blk_no].sect[i].free = 1;
      MLC_nand_blk[blk_no].sect[i].valid = 0;
      MLC_nand_blk[blk_no].sect[i].lsn = -1;
    }
    
    for(i = 0; i < M_PAGE_NUM_PER_BLK; i++){
      MLC_nand_blk[blk_no].page_status[i] = -1; // 0: data, 1: map table
    }
  }
  
  SLC_head = SLC_tail = & SLC_nand_blk[0];
  SLC_ARR_LEN = SLC_blk_num;
  SLC_COLD_ARR_LEN = SLC_ARR_LEN * Comb_SLC_Hot_ratio;
  SLC_HOT_ARR_LEN = SLC_ARR_LEN * (1 - Comb_SLC_Hot_ratio);
  
  SLC_cold_head = SLC_cold_tail = &SLC_nand_blk[SLC_COLD_ARR_LEN];
  free_SLC_blk_num = nand_SLC_blk_num;
  free_MLC_blk_num = nand_MLC_blk_num;
  free_blk_idx =0;
 
  MLC_nand_pbn_2_lpn_in_CMT_arr=(int *)malloc( sizeof(int) * nand_MLC_blk_num);
  MLC_nand_ppn_2_lpn_in_CMT_arr=(int *)malloc( sizeof(int) * nand_MLC_blk_num * M_PAGE_NUM_PER_BLK);
  if (MLC_nand_pbn_2_lpn_in_CMT_arr == NULL || MLC_nand_ppn_2_lpn_in_CMT_arr == NULL )
  {
  	return -1;
  }
  memset(MLC_nand_ppn_2_lpn_in_CMT_arr,0,sizeof(int) * nand_MLC_blk_num * M_PAGE_NUM_PER_BLK);
  memset(MLC_nand_pbn_2_lpn_in_CMT_arr,0,sizeof(int) * nand_MLC_blk_num );
  /********MLC Wear Level***********/
  Min_N_Prime=FindMinPrime(nand_MLC_blk_num);
  Liner_S=(int) nand_MLC_blk_num * 0.5;
  Liner_L=Liner_S;
  MLC_all_nand_ecn_counts=0;
  MLC_last_called_wear_num = MLC_called_wear_num = 0;
  MLC_wear_level_threshold = 0;
#ifdef DEBUG
  printf("MLC blk_num is %d\tMinPrime is %d\n",nand_MLC_blk_num,Min_N_Prime);
#endif
  MLC_last_blk_pc=0;
  
  mix_nand_stat_reset();
  return 0;
}

/*****************MIXSSD READ***********************/
_u8 SLC_nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = S_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0;

  if(pbn >= nand_SLC_blk_num){
    printf("psn: %d, pbn: %d, nand_SLC_blk_num: %d\n", psn, pbn, nand_SLC_blk_num);
  }

  ASSERT(S_OFF_F_SECT(psn) == 0);
  if(SLC_nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < 4224 ; i++){
      for(j =0; j < 256;j++){
        if(SLC_nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(SLC_nand_blk[pbn].state.free == 0);	// block should be written with something

  if (isGC == 1) {
    for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {

      if((SLC_nand_blk[pbn].sect[pin + i].free == 0) &&
         (SLC_nand_blk[pbn].sect[pin + i].valid == 1)) {
        lsns[valid_sect_num] = SLC_nand_blk[pbn].sect[pin + i].lsn;
        valid_sect_num++;
      }
    }

    if(valid_sect_num == 3){
      for(i = 0; i<S_SECT_NUM_PER_PAGE; i++){
        printf("pbn: %d, pin %d: %d, free: %d, valid: %d\n", 
            pbn, i, pin+i, SLC_nand_blk[pbn].sect[pin+i].free, SLC_nand_blk[pbn].sect[pin+i].valid);

      }
      exit(0);
    }

  } else if (isGC == 2) {
    for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {
        if (SLC_nand_blk[pbn].sect[pin + i].free == 0 &&
            SLC_nand_blk[pbn].sect[pin + i].valid == 1) {
          ASSERT(SLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
          valid_sect_num++;
        } else {
          lsns[i] = -1;
        }
      }
    }
  } 

  else { // every sector should be "valid", "not free"   
    for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {

        ASSERT(SLC_nand_blk[pbn].sect[pin + i].free == 0);
        ASSERT(SLC_nand_blk[pbn].sect[pin + i].valid == 1);
        ASSERT(SLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
        valid_sect_num++;
      }
      else{
        printf("lsns[%d]: %d shouldn't be -1\n", i, lsns[i]);
        exit(0);
      }
    }
  }
  
  if (isGC) {
    if (valid_sect_num > 0) {
      nand_stat(SLC_GC_PAGE_READ);
    }
  } else {
    nand_stat(SLC_PAGE_READ);
  }
  
  return valid_sect_num;
}

_u8 MLC_nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC)
{
  blk_t pbn = M_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = M_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0;

  if(pbn >= nand_MLC_blk_num){
    printf("psn: %d, pbn: %d, nand_MLC_blk_num: %d\n", psn, pbn, nand_MLC_blk_num);
  }

  ASSERT(M_OFF_F_SECT(psn) == 0);
  if(MLC_nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < 4224 ; i++){
      for(j =0; j < 1024;j++){
        if(MLC_nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(MLC_nand_blk[pbn].state.free == 0);	// block should be written with something

  if (isGC == 1) {
    for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {

      if((MLC_nand_blk[pbn].sect[pin + i].free == 0) &&
         (MLC_nand_blk[pbn].sect[pin + i].valid == 1)) {
        lsns[valid_sect_num] = MLC_nand_blk[pbn].sect[pin + i].lsn;
        valid_sect_num++;
      }
    }

    if(valid_sect_num == 7){
      for(i = 0; i<M_SECT_NUM_PER_PAGE; i++){
        printf("pbn: %d, pin %d: %d, free: %d, valid: %d\n", 
            pbn, i, pin+i, MLC_nand_blk[pbn].sect[pin+i].free, MLC_nand_blk[pbn].sect[pin+i].valid);

      }
      exit(0);
    }

  } else if (isGC == 2) {
    for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {
        if (MLC_nand_blk[pbn].sect[pin + i].free == 0 &&
            MLC_nand_blk[pbn].sect[pin + i].valid == 1) {
          ASSERT(MLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
          valid_sect_num++;
        } else {
          lsns[i] = -1;
        }
      }
    }
  } 

  else { // every sector should be "valid", "not free"   
    for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {

        ASSERT(MLC_nand_blk[pbn].sect[pin + i].free == 0);
        ASSERT(MLC_nand_blk[pbn].sect[pin + i].valid == 1);
        ASSERT(MLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
        valid_sect_num++;
      }
      else{
        printf("lsns[%d]: %d shouldn't be -1\n", i, lsns[i]);
        exit(0);
      }
    }
  }
  
  if (isGC) {
    if (valid_sect_num > 0) {
      nand_stat(MLC_GC_PAGE_READ);
    }
  } else {
    nand_stat(MLC_PAGE_READ);
  }
  
  return valid_sect_num;
}

/*****************MIXSSD WRITE***********************/
_u8 MLC_nand_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = M_BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = M_IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, valid_sect_num = 0;
  
  
  if(pbn >= nand_MLC_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_MLC_blk_num);
  ASSERT(M_OFF_F_SECT(psn) == 0);

  if(map_flag == 2) {
        MLC_nand_blk[pbn].page_status[pin/M_SECT_NUM_PER_PAGE] = 1; // 1 for map table
        translate_map_write_num ++ ;
  }
  else{
    MLC_nand_blk[pbn].page_status[pin/M_SECT_NUM_PER_PAGE] = 0; // 0 for data 
  }

  for (i = 0; i <M_SECT_NUM_PER_PAGE; i++) {

    if (lsns[i] != -1) {

      if(MLC_nand_blk[pbn].state.free == 1) {
        printf("blk num = %d",pbn);
      }

      ASSERT(MLC_nand_blk[pbn].sect[pin + i].free == 1);
      
      MLC_nand_blk[pbn].sect[pin + i].free = 0;			
      MLC_nand_blk[pbn].sect[pin + i].valid = 1;			
      MLC_nand_blk[pbn].sect[pin + i].lsn = lsns[i];	
      MLC_nand_blk[pbn].fpc--;  
      MLC_nand_blk[pbn].lwn = pin + i;	
      valid_sect_num++;
    }
    else{
      printf("lsns[%d] do not have any lsn\n", i);
    }
  }
  
  ASSERT(MLC_nand_blk[pbn].fpc >= 0);

  if (isGC) {
    nand_stat(MLC_GC_PAGE_WRITE);
  } else {
    nand_stat(MLC_PAGE_WRITE);
  }

  return valid_sect_num;
}

_u8 SLC_nand_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = S_IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, valid_sect_num = 0;


  if(pbn >= nand_SLC_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_SLC_blk_num);
  ASSERT(S_OFF_F_SECT(psn) == 0);

  if(map_flag == 2) {
        SLC_nand_blk[pbn].page_status[pin/S_SECT_NUM_PER_PAGE] = 1; // 1 for map table
  }
  else{
    SLC_nand_blk[pbn].page_status[pin/S_SECT_NUM_PER_PAGE] = 0; // 0 for data 
  }

  for (i = 0; i <S_SECT_NUM_PER_PAGE; i++) {

    if (lsns[i] != -1) {

      if(SLC_nand_blk[pbn].state.free == 1) {
        printf("blk num = %d",pbn);
      }

      ASSERT(SLC_nand_blk[pbn].sect[pin + i].free == 1);
      
      SLC_nand_blk[pbn].sect[pin + i].free = 0;			
      SLC_nand_blk[pbn].sect[pin + i].valid = 1;			
      SLC_nand_blk[pbn].sect[pin + i].lsn = lsns[i];	
      SLC_nand_blk[pbn].fpc--;  
      SLC_nand_blk[pbn].lwn = pin + i;	
      valid_sect_num++;
    }
    else{
      printf("lsns[%d] do not have any lsn\n", i);
    }
  }
  
  ASSERT(SLC_nand_blk[pbn].fpc >= 0);

  if (isGC) {
    nand_stat(SLC_GC_PAGE_WRITE);
  } else {
    nand_stat(SLC_PAGE_WRITE);
  }

  return valid_sect_num;
}

/*****************MIXSSD ERASE***********************/
void SLC_nand_erase (_u32 blk_no)
{
  int i;

  ASSERT(blk_no < nand_SLC_blk_num);

  ASSERT(SLC_nand_blk[blk_no].fpc <= S_SECT_NUM_PER_BLK);

  if(SLC_nand_blk[blk_no].state.free != 0){ printf("debug\n"); }

  ASSERT(SLC_nand_blk[blk_no].state.free == 0);
  
  SLC_nand_blk[blk_no].state.free = 1;
  SLC_nand_blk[blk_no].state.ec++;
  SLC_nand_blk[blk_no].fpc = S_SECT_NUM_PER_BLK;
  SLC_nand_blk[blk_no].ipc = 0;
  SLC_nand_blk[blk_no].lwn = -1;

  
  for(i = 0; i<S_SECT_NUM_PER_BLK; i++){
    SLC_nand_blk[blk_no].sect[i].free = 1;
    SLC_nand_blk[blk_no].sect[i].valid = 0;
    SLC_nand_blk[blk_no].sect[i].lsn = -1;
  }

  //initialize/reset page status 
  for(i = 0; i < S_PAGE_NUM_PER_BLK; i++){
    SLC_nand_blk[blk_no].page_status[i] = -1;
  }
  
//#ifdef DEBUG
  //printf("擦除的块号＝%d\n",blk_no);
//#endif
  free_SLC_blk_num++;

  nand_stat(SLC_BLOCK_ERASE);
}

void MLC_nand_erase (_u32 blk_no)
{
  int i;

  ASSERT(blk_no < nand_MLC_blk_num);

  ASSERT(MLC_nand_blk[blk_no].fpc <= M_SECT_NUM_PER_BLK);

  if(MLC_nand_blk[blk_no].state.free != 0){ printf("debug\n"); }

  ASSERT(MLC_nand_blk[blk_no].state.free == 0);

  MLC_nand_blk[blk_no].state.free = 1;
  MLC_nand_blk[blk_no].state.ec++;
  MLC_nand_blk[blk_no].fpc = M_SECT_NUM_PER_BLK;
  MLC_nand_blk[blk_no].ipc = 0;
  MLC_nand_blk[blk_no].lwn = -1;


  for(i = 0; i<M_SECT_NUM_PER_BLK; i++){
    MLC_nand_blk[blk_no].sect[i].free = 1;
    MLC_nand_blk[blk_no].sect[i].valid = 0;
    MLC_nand_blk[blk_no].sect[i].lsn = -1;
  }

  //initialize/reset page status 
  for(i = 0; i < M_PAGE_NUM_PER_BLK; i++){
    MLC_nand_blk[blk_no].page_status[i] = -1;
  }

  free_MLC_blk_num++;

  nand_stat(MLC_BLOCK_ERASE);
}

/*****************MIXSSD INVALID***********************/
void SLC_nand_invalidate (_u32 psn, _u32 lsn)
{
  _u32 pbn = S_BLK_F_SECT(psn);
  _u16 pin = S_IND_F_SECT(psn);
  if(pbn > nand_SLC_blk_num ) return;

  ASSERT(pbn < nand_SLC_blk_num);
  ASSERT(SLC_nand_blk[pbn].sect[pin].free == 0);
  if(SLC_nand_blk[pbn].sect[pin].valid != 1) { printf("debug"); }
  ASSERT(SLC_nand_blk[pbn].sect[pin].valid == 1);

  if(SLC_nand_blk[pbn].sect[pin].lsn != lsn){
    ASSERT(0);
  }

  ASSERT(SLC_nand_blk[pbn].sect[pin].lsn == lsn);
  
  SLC_nand_blk[pbn].sect[pin].valid = 0;
  SLC_nand_blk[pbn].ipc++;

  ASSERT(SLC_nand_blk[pbn].ipc <= S_SECT_NUM_PER_BLK);
}

void MLC_nand_invalidate (_u32 psn, _u32 lsn)
{
  _u32 pbn = M_BLK_F_SECT(psn);
  _u16 pin = M_IND_F_SECT(psn);
  if(pbn > nand_MLC_blk_num ) return;

  ASSERT(pbn < nand_MLC_blk_num);
  ASSERT(MLC_nand_blk[pbn].sect[pin].free == 0);
  if(MLC_nand_blk[pbn].sect[pin].valid != 1) { printf("debug"); }
  ASSERT(MLC_nand_blk[pbn].sect[pin].valid == 1);

  if(MLC_nand_blk[pbn].sect[pin].lsn != lsn){
    ASSERT(0);
  }

  ASSERT(MLC_nand_blk[pbn].sect[pin].lsn == lsn);
  
  MLC_nand_blk[pbn].sect[pin].valid = 0;
  MLC_nand_blk[pbn].ipc++;

  ASSERT(MLC_nand_blk[pbn].ipc <= M_SECT_NUM_PER_BLK);
}


/*****************MIXSSD GET FREE BLK********************/
_u32 nand_get_SLC_free_blk (int isGC) 
{
  _u32 blk_no=-1 , i;
  int flag = 0,flag1=0;
  flag = 0;
  flag1 = 0;

  MIN_ERASE = 9999999;
  //in case that there is no avaible free block -> GC should be called !
  if ((isGC == 0) && (min_fb_num >= free_SLC_blk_num)) {
    printf("min_fb_num: %d\n", min_fb_num);
    assert(0);
  }

   blk_no = SLC_head - SLC_nand_blk;
   if(SLC_nand_blk[blk_no].state.free==1){
      flag=1;
   } 
      
  if ( flag == 1) {
        flag = 0;
        ASSERT(SLC_nand_blk[blk_no].fpc == S_SECT_NUM_PER_BLK);
        ASSERT(SLC_nand_blk[blk_no].ipc == 0);
        ASSERT(SLC_nand_blk[blk_no].lwn == -1);
        SLC_nand_blk[blk_no].state.free = 0;

        free_blk_idx = blk_no;
        free_SLC_blk_num--;
 //~ #ifdef DEBUG
        //~ printf("获取成功，块号：%d\n",blk_no);
 //~ #endif
        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }

  return -1;
}

_u32 nand_get_MLC_free_blk (int isGC) 
{
  _u32 blk_no = -1, i;
  int flag = 0,flag1=0;
  flag = 0;
  flag1 = 0;

  MIN_ERASE = 9999999;
  //in case that there is no avaible free block -> GC should be called !
  if ((isGC == 0) && (min_fb_num >= free_MLC_blk_num)) {
    //printf("min_fb_num: %d\n", min_fb_num);
    return -1;
  }

  for(i =0 ; i < nand_MLC_blk_num; i++) 
  {
    if (MLC_nand_blk[i].state.free == 1) {
      flag1 = 1;

      if ( MLC_nand_blk[i].state.ec < MIN_ERASE ) {
            blk_no = i;
            MIN_ERASE = MLC_nand_blk[i].state.ec;
            flag = 1;
      }
    }
  }
  if(flag1 != 1){
    printf("no free block left=%d",free_MLC_blk_num);
    
  ASSERT(0);
  }
  if ( flag == 1) {
        flag = 0;
        ASSERT(MLC_nand_blk[blk_no].fpc == M_SECT_NUM_PER_BLK);
        ASSERT(MLC_nand_blk[blk_no].ipc == 0);
        ASSERT(MLC_nand_blk[blk_no].lwn == -1);
        MLC_nand_blk[blk_no].state.free = 0;

        free_blk_idx = blk_no;
        free_MLC_blk_num--;
       // printf("MLC空闲块数：%d\n",free_MLC_blk_num);
        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }

  return -1;
}

/****************MIXSSD OOB READ************************************/
int SLC_nand_oob_read(_u32 psn)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = S_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i, valid_flag = 0;

  ASSERT(pbn < nand_SLC_blk_num);	// pbn shouldn't exceed max nand block number 

  for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
    if(SLC_nand_blk[pbn].sect[pin + i].free == 0){

      if(SLC_nand_blk[pbn].sect[pin + i].valid == 1){
        valid_flag = 1;
        break;
      }
      else{
        valid_flag = -1;
        break;
      }
    }
    else{
      valid_flag = 0;
      break;
    }
  }

  nand_stat(SLC_OOB_READ);
  
  return valid_flag;
}

int MLC_nand_oob_read(_u32 psn)
{
  blk_t pbn = M_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = M_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i, valid_flag = 0;

  ASSERT(pbn < nand_MLC_blk_num);	// pbn shouldn't exceed max nand block number 

  for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
    if(MLC_nand_blk[pbn].sect[pin + i].free == 0){

      if(MLC_nand_blk[pbn].sect[pin + i].valid == 1){
        valid_flag = 1;
        break;
      }
      else{
        valid_flag = -1;
        break;
      }
    }
    else{
      valid_flag = 0;
      break;
    }
  }

  nand_stat(MLC_OOB_READ);
  
  return valid_flag;
}

/*****************MIXSSD  NAND PRITN********************/
void mix_nand_stat_print(FILE *outFP)
{
  int i;
  fprintf(outFP, "\n");

  fprintf(outFP, "SLC FLASH Volume is %lf GB\n",(nand_SLC_blk_num * 128.0)/(1024*1024));
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, " SLC_Page read (#): %8u   ", SLC_stat_read_num);
  fprintf(outFP, " SLC_Page write (#): %8u   ", SLC_stat_write_num);
  fprintf(outFP, " SLC_Block erase (#): %8u\n", SLC_stat_erase_num);

  fprintf(outFP, " SLC_GC page read (#): %8u   ", SLC_stat_gc_read_num);
  fprintf(outFP, " SLC_GC page write (#): %8u\n", SLC_stat_gc_write_num);
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP,"MLC FLASH Volume is %lf GB\n",(nand_MLC_blk_num * 512.0)/(1024 * 1024));
  fprintf(outFP, " MLC_Page read (#): %8u   ", MLC_stat_read_num);
  fprintf(outFP, " MLC_Page write (#): %8u   ", MLC_stat_write_num);
  fprintf(outFP, " MLC_Block erase (#): %8u\n", MLC_stat_erase_num);

  fprintf(outFP, " MLC_GC page read (#): %8u   ", MLC_stat_gc_read_num);
  fprintf(outFP, " MLC_GC page write (#): %8u\n", MLC_stat_gc_write_num);
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, "Real CallFsim write sect num is %d\tconvert to Volume is %lfGB\n", real_data_write_sect_num,real_data_write_sect_num*1.0/(1024*1024*2));
  fprintf(outFP, "Page aligment(2K) padding sector is %d\tconvert to Volume is %lfGB\n",page_align_padding_sect_num,page_align_padding_sect_num*1.0/(1024*1024*2));
  fprintf(outFP, "map(2K page size) write num is %d\tconvert to Volume is %lfGB\n", translate_map_write_num,translate_map_write_num*2.0/(1024*1024));     
  fprintf(outFP, "inner map(2K page size) write data-blk GC trigger num is %d\tconvert to Volume is %lfGB\t occupy rate:%lf\n", 
                                                                              data_blk_gc_trigger_map_write_num,
                                                                              data_blk_gc_trigger_map_write_num *2.0/((1024*1024)),
                                                                              data_blk_gc_trigger_map_write_num*1.0/translate_map_write_num);
  fprintf(outFP, "inner map(2K page size) write map-blk GC trigger num is %d\tconvert to Volume is %lfGB\t occupy rate:%lf\n", 
                                                                              map_blk_gc_trigger_map_write_num,
                                                                              map_blk_gc_trigger_map_write_num *2.0/((1024*1024)),
                                                                              map_blk_gc_trigger_map_write_num*1.0/translate_map_write_num);
  fprintf(outFP, "SLC_to_MLC_counts (#): %8u\tconvert to Volume is %lf GB\n", SLC_to_MLC_counts, (SLC_to_MLC_counts * 4.0)/(1024 * 1024));
  fprintf(outFP, "SLC_to_SLC_counts (#): %8u\tconvert to Volume is %lf GB\n", SLC_to_SLC_counts, (SLC_to_SLC_counts * 4.0)/(1024 * 1024));
  fprintf(outFP, "MLC called Wear Level num is %d\n",MLC_called_wear_num);
  fprintf(outFP,"--------------------------------------------------------------\n");
  
  fprintf(outFP,"**************SLC ECN VALUE STATIC*****************************\n");
  for(i=0; i < nand_SLC_blk_num ; i++){
	fprintf(outFP,"SLCNANDBLK ECN %d\n",SLC_nand_blk[i].state.ec);
  }
  fprintf(outFP,"**************MLC ECN VALUE STATIC*****************************\n");
  
  for(i=0; i < nand_MLC_blk_num ; i++){
	fprintf(outFP,"MLCNANDBLK ECN %d\n",MLC_nand_blk[i].state.ec);
  }
}

/************************************
 *  this code may be to delete
 * **********************************/
_u32 SLC_nand_get_cold_free_blk (int isGC) 
{
  _u32 blk_no=-1 , i;
  int flag = 0,flag1=0;
  flag = 0;
  flag1 = 0;
  MIN_ERASE = 9999999;

   blk_no = SLC_cold_head - &SLC_nand_blk[0];
   if(SLC_nand_blk[blk_no].state.free==1){
      flag=1;
   } 
      
  if ( flag == 1) {
        flag = 0;
        ASSERT(SLC_nand_blk[blk_no].fpc == S_SECT_NUM_PER_BLK);
        ASSERT(SLC_nand_blk[blk_no].ipc == 0);
        ASSERT(SLC_nand_blk[blk_no].lwn == -1);
        SLC_nand_blk[blk_no].state.free = 0;

        free_blk_idx = blk_no;
        free_cold_blk_num--;
       // printf("获取cold块成功，块号：%d\n",blk_no);
        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }
  return -1;
}

/**************************Wear Level Function*************************************************/
_u32 MLC_find_switch_cold_blk_method1(int victim_blk_no)
{
	int i,min_bitmap_value = M_PAGE_NUM_PER_BLK;
	
	static_MLC_pbn_map_entry_in_CMT();
	
	for(i = 0 ;i < nand_MLC_blk_num; i++){
		if(MLC_nand_blk_bit_map[i] < min_bitmap_value)
			min_bitmap_value = MLC_nand_blk_bit_map[i];
	}
#ifdef DEBUG
	if(min_bitmap_value > 0){
		printf("MLC nand_blk_bit_map value all larger than 0!\n");
	}
#endif
	while(1){
		Liner_L=(Liner_S+Liner_L) % Min_N_Prime;
		if(Liner_L < nand_MLC_blk_num ) {
			// init time my_global_nand_blk_wear_ave is 0!
			if (MLC_global_nand_blk_wear_ave < MLC_min_nand_wear_ave && MLC_nand_blk[Liner_L].fpc ==0 
				&& MLC_nand_blk[Liner_L].state.free == 0 && MLC_nand_blk[Liner_L].ipc == 0 ){
				break;
			}
			if(MLC_nand_blk[Liner_L].state.ec < MLC_global_nand_blk_wear_ave + MLC_min_nand_wear_ave
				&& MLC_nand_blk_bit_map[Liner_L] == min_bitmap_value 
				&& MLC_nand_blk[Liner_L].state.free ==0 && MLC_nand_blk[Liner_L].fpc == 0 && MLC_nand_blk[Liner_L].ipc == 0){
					break;
			}
		}

	}
	return (_u32)Liner_L;
}
/********************************************
 *  Cycle run find cold data blk with min ecn
 ******************************************/
_u32 MLC_find_switch_cold_blk_method2(int victim_blk_no)
{
	int i,min_bitmap_value = M_PAGE_NUM_PER_BLK;
	static_MLC_pbn_map_entry_in_CMT();
	
	for(i = 0 ;i < nand_MLC_blk_num; i++){
		if(MLC_nand_blk_bit_map[i] < min_bitmap_value)
			min_bitmap_value = MLC_nand_blk_bit_map[i];
	}
#ifdef DEBUG
	if(min_bitmap_value > 0){
		printf("nand_blk_bit_map value all larger than 0!\n");
	}
#endif
	while(1){
		if( MLC_last_blk_pc >= nand_MLC_blk_num )
			MLC_last_blk_pc = 0;

		if (MLC_global_nand_blk_wear_ave < MLC_min_nand_wear_ave && MLC_nand_blk[MLC_last_blk_pc].fpc ==0 
				&& MLC_nand_blk[MLC_last_blk_pc].state.free == 0){
				break;
		}
		
		if( MLC_nand_blk[MLC_last_blk_pc].state.ec < (MLC_global_nand_blk_wear_ave + MLC_min_nand_wear_ave)
			&& MLC_nand_blk_bit_map[MLC_last_blk_pc] == min_bitmap_value 
			&& MLC_nand_blk[MLC_last_blk_pc].state.free ==0 && MLC_nand_blk[MLC_last_blk_pc].fpc ==0 ) {
			break;
		}
		
		MLC_last_blk_pc++;
	}
	return MLC_last_blk_pc;
}

/*
* add zhoujie 11-13
* funciton : to select Wear-level threshold and update
*/
void Select_Wear_Level_Threshold(int Type)
{
	 double temp;
	 switch(Type){
		case STATIC_THRESHOLD: 
			MLC_wear_level_threshold = static_wear_threshold;
			break;
		case DYNAMIC_THRESHOLD:
			if(MLC_stat_erase_num % Wear_Session_Cycle == 0 && MLC_called_wear_num != 0 ){
				temp = (MLC_called_wear_num - MLC_last_called_wear_num) *1.0 / nand_MLC_blk_num;
				MLC_wear_level_threshold = sqrt(100 / dynamic_wear_beta) * sqrt(temp * MLC_wear_level_threshold);
	#ifdef DEBUG
				printf("----------------------------------\n");
				printf("curr stat_erase_num is %d\t,Session Cycle is %d\n",MLC_stat_erase_num,Wear_Session_Cycle);
				printf("Session called wear num is %d\n",MLC_called_wear_num-MLC_last_called_wear_num);
				printf("my_wear_level_threshold is %lf\n",MLC_wear_level_threshold);
				printf("----------------------------------\n");
				
	#endif
				MLC_last_called_wear_num = MLC_called_wear_num;
			}

			break;
		case  AVE_ADD_N_VAR:
			MLC_nand_blk_ecn_std_var_static();
			MLC_wear_level_threshold = N_wear_var * MLC_global_nand_blk_wear_var;
			break;
		default : break;
	 }
}

