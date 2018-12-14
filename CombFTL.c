/********************************************************************
 * Name : CombFTL Module
 * Author : zhoujie
 * Date :10.12.2018
 * Thanks for YangMingBo contribution and help
 ********************************************************************/ 
#include <stdlib.h>
#include <string.h>
#include "flash.h"
#include "CombFTL.h"
#include "ssd_interface.h"
#include "disksim_global.h"
#include "HBFTL.h"

static struct mix_omap_dir *MLC_mapdir;
static blk_t extra_blk_num;
// [0]--data [1]---map
static _u32 free_SLC_blk_no[2];
static _u32 free_MLC_blk_no[2];
static _u16 free_SLC_page_no[2];
static _u16 free_MLC_page_no[2];
static _u16 free_cold_page_no;
static _u16 free_cold_blk_no;

#define HOT_FREE_MIN 4
#define COLD_FREE_MIN 4
extern int SLC_HOT_ARR_LEN; 
extern int SLC_COLD_ARR_LEN;
extern int SLC_ARR_LEN;


extern int merge_switch_num;
extern int merge_partial_num;
extern int merge_full_num;
extern int SLC_to_MLC_counts;

extern int MLC_page_num_for_2nd_map_table;
extern struct SLC_nand_blk_info * SLC_head;//in this region --> hot-head
extern struct SLC_nand_blk_info * SLC_tail;//in this region --> hot-tail

//MLC function
_u32 Comb_MLC_opm_gc_cost_benefit();
size_t Comb_MLC_opm_write(sect_t lsn, sect_t size, int mapdir_flag);
_u32 Comb_MLC_opm_read(sect_t lsn, sect_t size, int mapdir_flag);
int Comb_MLC_opm_gc_get_free_blk(int small, int mapdir_flag);
int Comb_MLC_opm_gc_run(int small, int mapdir_flag);
void Comb_MLC_opm_wear_level(int target_blk_no);
//SLC function
size_t Comb_SLC_opm_write(sect_t lsn, sect_t size, int mapdir_flag);
size_t Comb_SLC_opm_read(sect_t lsn, sect_t size, int mapdir_flag);
void Comb_SLC_data_move(int blk);
void Comb_SLC_Hot_Circular_Que();
void Comb_SLC_Hot2Cold(int blk);
void Comb_SLC_Cold_Circular_Que();
/*********************MLC FUNCTION*******************************/
_u32 Comb_MLC_opm_gc_cost_benefit()
{
  int max_cb = 0;
  int blk_cb;

  _u32 max_blk = -1, i;

  for (i = 0; i < nand_MLC_blk_num; i++) {
    if(i == free_MLC_blk_no[0] || i == free_MLC_blk_no[1]){
      continue;
    }

    blk_cb = MLC_nand_blk[i].ipc;

    
    if (blk_cb > max_cb) {
      max_cb = blk_cb;
      max_blk = i;
    }
  }
  ASSERT(max_blk != -1);
  ASSERT(MLC_nand_blk[max_blk].ipc > 0);
  return max_blk;
}
//read
size_t Comb_MLC_opm_read(sect_t lsn, sect_t size, int mapdir_flag)
{
  int i;
  int lpn = lsn/M_SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/M_SECT_NUM_PER_PAGE; // size in page 
  int sect_num;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 

  sect_t lsns[M_SECT_NUM_PER_PAGE];

  ASSERT(lpn < MLC_opagemap_num);
  ASSERT(lpn + size_page <= MLC_opagemap_num);

  memset (lsns, 0xFF, sizeof (lsns));

  sect_num = (size < M_SECT_NUM_PER_PAGE) ? size : M_SECT_NUM_PER_PAGE;

  if(mapdir_flag == 2){
    s_psn = MLC_mapdir[lpn].ppn * M_SECT_NUM_PER_PAGE;
  }
  else s_psn = MLC_opagemap[lpn].ppn * M_SECT_NUM_PER_PAGE;

  s_lsn = lpn * M_SECT_NUM_PER_PAGE;

  for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
    lsns[i] = s_lsn + i;
  }

  //~ if(mapdir_flag == 2){
    //~ map_pg_read++;
  //~ }
  size = MLC_nand_page_read(s_psn, lsns, 0);
  ASSERT(size == M_SECT_NUM_PER_PAGE);
  return sect_num;
}

//write
size_t Comb_MLC_opm_write(sect_t lsn, sect_t size, int mapdir_flag)  
{
  int i;
  int lpn = lsn/M_SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/M_SECT_NUM_PER_PAGE; // size in page 
  int ppn;
  int small;

  sect_t lsns[M_SECT_NUM_PER_PAGE];
  int sect_num = M_SECT_NUM_PER_PAGE;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 
  sect_t s_psn1;

  ASSERT(lpn < MLC_opagemap_num);
  ASSERT(lpn + size_page <= MLC_opagemap_num);

  s_lsn = lpn * M_SECT_NUM_PER_PAGE;

  if(mapdir_flag == 2) //map page
    small = 0;
  else if ( mapdir_flag == 1) //data page
    small = 1;
  else{
    printf("something corrupted");
    exit(0);
  }

  if (free_MLC_page_no[small] >= M_SECT_NUM_PER_BLK) {
    if ((free_MLC_blk_no[small] = nand_get_MLC_free_blk(0)) == -1) {
      int j = 0;
      while (free_MLC_blk_num < MIN_FREE_BLK_NUM ){
        j += Comb_MLC_opm_gc_run(small, mapdir_flag);
      }
      Comb_MLC_opm_gc_get_free_blk(small, mapdir_flag);
    }else{
      free_MLC_page_no[small] = 0;
    }
  }
  memset (lsns, 0xFF, sizeof (lsns));
  s_psn = M_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]);

  if(s_psn % M_SECT_NUM_PER_PAGE != 0){
    printf("s_psn: %d\n", s_psn);
  }
  ppn = s_psn / M_SECT_NUM_PER_PAGE;

  if (MLC_opagemap[lpn].free == 0) {
    s_psn1 = MLC_opagemap[lpn].ppn * M_SECT_NUM_PER_PAGE;
    for(i = 0; i<M_SECT_NUM_PER_PAGE; i++){
      MLC_nand_invalidate(s_psn1 + i, s_lsn + i);
    } 
    nand_stat(MLC_OOB_WRITE );
  }else {
    MLC_opagemap[lpn].free = 0;
  }

  for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
    lsns[i] = s_lsn + i;
  }

  if(mapdir_flag == 2) {
    MLC_mapdir[lpn].ppn = ppn;
    MLC_opagemap[lpn].ppn = ppn;
  }else {
    MLC_opagemap[lpn].ppn = ppn;
  }

  free_MLC_page_no[small] += M_SECT_NUM_PER_PAGE;
  MLC_nand_page_write(s_psn, lsns, 0, mapdir_flag);
  return sect_num;
}

int Comb_MLC_opm_gc_get_free_blk(int small, int mapdir_flag)
{
  if (free_MLC_page_no[small] >= M_SECT_NUM_PER_BLK) {

    free_MLC_blk_no[small] = nand_get_MLC_free_blk(1);

    free_MLC_page_no[small] = 0;

    return -1;
  }
  
  return 0;
}

int Comb_MLC_opm_gc_run(int small, int mapdir_flag)
{
  blk_t victim_blk_no;
  int merge_count;
  int i,z, j,m,q, benefit = 0;
  int k,old_flag,temp_arr[M_PAGE_NUM_PER_BLK],temp_arr1[M_PAGE_NUM_PER_BLK],map_arr[M_PAGE_NUM_PER_BLK]; 
  int valid_flag,pos;

  _u32 copy_lsn[M_SECT_NUM_PER_PAGE], copy[M_SECT_NUM_PER_PAGE];
  _u16 valid_sect_num, l, s;
  victim_blk_no = Comb_MLC_opm_gc_cost_benefit();
  memset(copy_lsn, 0xFF, sizeof (copy_lsn));

  s = k = M_OFF_F_SECT(free_MLC_page_no[small]);
  if(!((s == 0) && (k == 0))){
    printf("s && k should be 0\n");
    exit(0);
  }
 
  small = -1;
  for( q = 0; q < M_PAGE_NUM_PER_BLK; q++){
    if(MLC_nand_blk[victim_blk_no].page_status[q] == 1){ //map block
      for( q = 0; q  < 128; q++) {
        if(MLC_nand_blk[victim_blk_no].page_status[q] == 0 ){
          printf("something corrupted1=%d",victim_blk_no);
        }
      }
      small = 0;
      break;
    } 
    else if(MLC_nand_blk[victim_blk_no].page_status[q] == 0){ //data block
      for( q = 0; q  < 128; q++) {
        if(MLC_nand_blk[victim_blk_no].page_status[q] == 1 ){
          printf("something corrupted2",victim_blk_no);
        }
      }
      small = 1;
      break;
    }
  }

  ASSERT ( small == 0 || small == 1);
  pos = 0;
  merge_count = 0;
  for (i = 0; i < M_PAGE_NUM_PER_BLK; i++) {
    valid_flag = MLC_nand_oob_read( M_SECTOR(victim_blk_no, i * M_SECT_NUM_PER_PAGE));
    if(valid_flag == 1){
        valid_sect_num = MLC_nand_page_read( M_SECTOR(victim_blk_no, i * M_SECT_NUM_PER_PAGE), copy, 1);
        merge_count++;
        ASSERT(valid_sect_num == 8);
        k=0;
        for (j = 0; j < valid_sect_num; j++) {
          copy_lsn[k] = copy[j];
          k++;
        }
        benefit += Comb_MLC_opm_gc_get_free_blk(small, mapdir_flag);
		if(MLC_nand_blk[victim_blk_no].page_status[i] == 1){                       
           MLC_mapdir[(copy_lsn[s]/M_SECT_NUM_PER_PAGE)].ppn  = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]));
           MLC_opagemap[copy_lsn[s]/M_SECT_NUM_PER_PAGE].ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]));
           MLC_nand_page_write(M_SECTOR(free_MLC_blk_no[small],free_MLC_page_no[small]) & (~M_OFF_MASK_SECT), copy_lsn, 1, 2);
           map_blk_gc_trigger_map_write_num ++;
           free_MLC_page_no[small] += M_SECT_NUM_PER_PAGE;
         }else{
            MLC_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[s])].ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]));
            MLC_nand_page_write(M_SECTOR(free_MLC_blk_no[small],free_MLC_page_no[small]) & (~M_OFF_MASK_SECT), copy_lsn, 1, 1);
            free_MLC_page_no[small] += M_SECT_NUM_PER_PAGE;
            if((MLC_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_REAL) || (MLC_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_GHOST)) {
              delay_flash_update++;
            }else {
              map_arr[pos] = copy_lsn[s];
              pos++;
            } 
          }
    }//end-if-valid-flag
  }//end-for
  
  for(i=0;i < M_PAGE_NUM_PER_BLK;i++) {
      temp_arr[i]=-1;
  }
  k=0;
  for(i =0 ; i < pos; i++) {
      old_flag = 0;
      for( j = 0 ; j < k; j++) {
           if(temp_arr[j] == MLC_mapdir[((map_arr[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn) {
                if(temp_arr[j] == -1){
                      printf("something wrong");
                      ASSERT(0);
                }
                old_flag = 1;
                break;
           }
      }
      if( old_flag == 0 ) {
           temp_arr[k] = MLC_mapdir[((map_arr[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn;
           temp_arr1[k] = map_arr[i];
           k++;
      }
  }

  for (i=0; i < k; i++) {
	  if (free_MLC_page_no[0] >= M_SECT_NUM_PER_BLK) {
		if((free_MLC_blk_no[0] = nand_get_MLC_free_blk(1)) == -1){
			printf("we are in big trouble shudnt happen");
         }
		free_MLC_page_no[0] = 0;
	   }
	   
	   MLC_nand_page_read(temp_arr[i]*M_SECT_NUM_PER_PAGE,copy,1);
       for(m = 0; m<M_SECT_NUM_PER_PAGE; m++){
          MLC_nand_invalidate(MLC_mapdir[((temp_arr1[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn*M_SECT_NUM_PER_PAGE+m, copy[m]);
       } 
       nand_stat(MLC_OOB_WRITE);
	   MLC_mapdir[((temp_arr1[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn  = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[0], free_MLC_page_no[0]));
	   MLC_opagemap[((temp_arr1[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[0], free_MLC_page_no[0]));
	   MLC_nand_page_write(M_SECTOR(free_MLC_blk_no[0],free_MLC_page_no[0]) & (~M_OFF_MASK_SECT), copy, 1, 2);
	   data_blk_gc_trigger_map_write_num ++;
	   free_MLC_page_no[0] += M_SECT_NUM_PER_PAGE;
	 
  }
  if(merge_count == 0 ) 
    merge_switch_num++;
  else if(merge_count > 0 && merge_count < M_PAGE_NUM_PER_BLK)
    merge_partial_num++;
  else if(merge_count == M_PAGE_NUM_PER_BLK)
    merge_full_num++;
  else if(merge_count > M_PAGE_NUM_PER_BLK){
    printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,M_PAGE_NUM_PER_BLK);
    ASSERT(0);
  }

  MLC_nand_erase(victim_blk_no);
  //called wear - level function
   Select_Wear_Level_Threshold(Wear_Threshold_Type);
   if(MLC_nand_blk[victim_blk_no].state.ec > (int)(MLC_global_nand_blk_wear_ave + MLC_wear_level_threshold)){
		CFTL_MLC_opm_wear_level(victim_blk_no);
		MLC_called_wear_num ++;
#ifdef DEBUG
		switch(Wear_Threshold_Type){
				case STATIC_THRESHOLD: 
					printf("THRESHOLD TYPE is static threshold\n");
					break;
				case DYNAMIC_THRESHOLD:
					printf("THRESHOLD TYPE is dynamic threshold\n");
					break;
				case  AVE_ADD_N_VAR:
					printf("THRESHOLD TYPE is ave add %d * var\n",N_wear_var);
					break;
				default : assert(0);break;
		}
		printf("MLC called opm wear level %d\n",MLC_called_wear_num);
#endif
   }
  return (benefit + 1);
}

/**********************SLC FUNCTION***************************/
/******************************************
* Name : SLC_opm_write
* Date : 2018-12-10 
* author: ymb 
* param: lsn (逻辑扇区号)
*        size (扇区大小)
*        mapdir_flag (操作位置 2:映射块,1:数据块)
*        region_flag (数据写入冷热判断：0:hot )
* return value: 
* Attention : SLC only have data blk(mapdir_flag = 0)
***********************************************/
size_t Comb_SLC_opm_write(sect_t lsn, sect_t size, int mapdir_flag)  
{
    int i;
    int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
    int size_page = size/SECT_NUM_PER_PAGE; // size in page 
    int ppn;
    int small;

    sect_t lsns[SECT_NUM_PER_PAGE];
    int sect_num = SECT_NUM_PER_PAGE;

    sect_t s_lsn;	// starting logical sector number
    sect_t s_psn; // starting physical sector number 
    sect_t s_psn1;
    
    ASSERT(lpn < SLC_opagemap_num);
    ASSERT(lpn + size_page <= SLC_opagemap_num);
    s_lsn = lpn * S_SECT_NUM_PER_PAGE;
    
    memset (lsns, 0xFF, sizeof (lsns));
    ASSERT(mapdir_flag == 0);
    small = 1;
    //get free write blk region (SLC_haed is curr write blk num)
	if(free_SLC_page_no[small] >= S_SECT_NUM_PER_BLK){
		Comb_SLC_Hot_Circular_Que();
		free_SLC_blk_no[small] = nand_get_SLC_free_blk(0);
		free_SLC_page_no[small] = 0;
	}
    s_psn = S_SECTOR(free_SLC_blk_no[small], free_SLC_page_no[small]);
   
    if(s_psn % S_SECT_NUM_PER_PAGE != 0){
        printf("s_psn: %d\n", s_psn);
    }
    
    ppn = s_psn / S_SECT_NUM_PER_PAGE;
    //invalid old region page
    if(SLC_opagemap[lpn].free == 0){
		s_psn1 = SLC_opagemap[lpn].ppn * S_SECT_NUM_PER_PAGE;
		for(i=0 ;i <S_SECT_NUM_PER_PAGE;i++){
			SLC_nand_invalidate(s_psn1+i, s_lsn+i);
		}
		nand_stat(SLC_OOB_WRITE);
	}else{
		SLC_opagemap[lpn].free = 0;
	}
    
    for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
        lsns[i] = s_lsn + i;
    }
	//create new ppn --> lpn relation map
	SLC_opagemap[lpn].ppn = ppn;
    free_SLC_page_no[small] += S_SECT_NUM_PER_PAGE;
    
    SLC_nand_page_write(s_psn, lsns, 0, mapdir_flag);
    return sect_num;

}

/**************************************************
 * Attention SLC only have data (mapdir_flag =0)
 **************************************************/
size_t Comb_SLC_opm_read(sect_t lsn, sect_t size, int mapdir_flag)
{
  int i;
  int lpn = lsn/S_SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/S_SECT_NUM_PER_PAGE; // size in page 
  int sect_num;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 

  sect_t lsns[S_SECT_NUM_PER_PAGE];
  ASSERT(lpn < SLC_opagemap_num);
  ASSERT(lpn + size_page <= SLC_opagemap_num);
  memset (lsns, 0xFF, sizeof (lsns));

  sect_num = (size < S_SECT_NUM_PER_PAGE) ? size : S_SECT_NUM_PER_PAGE;
  s_psn = SLC_opagemap[lpn].ppn * S_SECT_NUM_PER_PAGE;
  s_lsn = lpn * S_SECT_NUM_PER_PAGE;

  for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
    lsns[i] = s_lsn + i;
  }

  size = SLC_nand_page_read(s_psn, lsns, 0);
  
  ASSERT(size == S_SECT_NUM_PER_PAGE);
  return sect_num;
}

/******************************************
* Name : Comb_SLC_data_move
* Date : 2018-12-10 
* author: ymb 
* param: blk (为SLC_nand_blk中的块索引)
* return value: 
* Attention : 
***********************************************/
void Comb_SLC_data_move(int blk)
{
    int i, valid_flag, valid_sect_num;
    int blkno, bcount, blk_index;
    double delay3;
    _u32 victim_blkno;
    _u32 copy_lsn[S_SECT_NUM_PER_PAGE];
	int last_MLC_lpn = -1;
    for(i=0; i < S_PAGE_NUM_PER_BLK; i++){
        valid_flag=SLC_nand_oob_read(S_SECTOR(blk, i*S_SECT_NUM_PER_PAGE));
        if(valid_flag == 1){
            valid_sect_num = SLC_nand_page_read(S_SECTOR(blk, i*S_SECT_NUM_PER_PAGE), copy_lsn, 1);
            ASSERT(valid_sect_num == 4);
            if(SLC_opagemap[S_BLK_PAGE_NO_SECT(copy_lsn[0])].count < 2){
				//this need to check free_cold_page_no
				ASSERT(free_cold_page_no <= S_SECT_NUM_PER_BLK);
				SLC_opagemap[S_BLK_PAGE_NO_SECT(copy_lsn[0])].ppn = S_BLK_PAGE_NO_SECT(S_SECTOR(free_cold_blk_no,free_cold_page_no));
				SLC_nand_page_write(S_SECTOR(free_cold_blk_no, free_cold_page_no) &(~S_OFF_MASK_SECT), copy_lsn, 1, 1);
				free_cold_page_no += S_SECT_NUM_PER_PAGE;
				SLC_opagemap[S_BLK_PAGE_NO_SECT(copy_lsn[0])].count += 1;
				SLC_to_SLC_counts ++;
			}else{
				SLC_opagemap[S_BLK_PAGE_NO_SECT(copy_lsn[0])].ppn = -1;
				SLC_opagemap[S_BLK_PAGE_NO_SECT(copy_lsn[0])].count = 0;
				SLC_opagemap[S_BLK_PAGE_NO_SECT(copy_lsn[0])].free = 1;
    		    blkno = (S_BLK_PAGE_NO_SECT(copy_lsn[0]) * S_SECT_NUM_PER_PAGE)/M_SECT_NUM_PER_PAGE;
			    blkno *= M_SECT_NUM_PER_PAGE;
		        bcount = M_SECT_NUM_PER_PAGE;
		        //direct to called outer function(this maybe some problem)
		        if(last_MLC_lpn != blkno){
					Write_2_MLC(blkno, bcount);
					SLC_to_MLC_counts ++;
				}
				last_MLC_lpn = blkno;
	        }
        }
    }
    victim_blkno=blk; 
    SLC_nand_erase(victim_blkno);   
}

/******************************************
* Name : Comb_SLC_Hot_Circular_Que
* Date : 2018-12-10 
* author: ymb 
* param: 
* return value: 
* Attention : 
***********************************************/
void Comb_SLC_Hot_Circular_Que()
{
    int SLC_blk_index;
    if(SLC_head >= SLC_tail){// equal only happen in init
        if(SLC_head - SLC_nand_blk <= SLC_HOT_ARR_LEN - HOT_FREE_MIN){
            SLC_head++;
        }
        else{
            SLC_blk_index = SLC_tail - SLC_nand_blk;
            SLC_head++;
            Comb_SLC_Hot2Cold(SLC_blk_index);
            SLC_tail++;
        }
        if(SLC_head - SLC_nand_blk == SLC_HOT_ARR_LEN)
            SLC_head = &SLC_nand_blk[0];
    }
    else{  // head < tail
        SLC_blk_index = SLC_tail - SLC_nand_blk;
        SLC_head++;
        Comb_SLC_Hot2Cold(SLC_blk_index);
        SLC_tail++;
        if(SLC_tail - SLC_nand_blk == SLC_HOT_ARR_LEN)
            SLC_tail = &SLC_nand_blk[0];
    }
}

/******************************************
* Name : Comb_SLC_Hot2Cold
* Date : 2018-12-10 
* author: ymb 
* param: blk (为SLC_nand_blk中的块索引)
* return value: 
* Attention : 
***********************************************/
void Comb_SLC_Hot2Cold(int blk)
{
    int i,valid_flag,valid_sect_num;
    int blkno, bcount;
    int blk_index;
    _u32 victim_blkno;
    _u32 copy_lsn[S_SECT_NUM_PER_PAGE];
    
    for(i=0;i<S_PAGE_NUM_PER_BLK;i++){
		while(free_cold_page_no >= S_SECT_NUM_PER_BLK)//if (free_cold_page_no >= S_SECT_NUM_PER_BLK) 
        {
			Comb_SLC_Cold_Circular_Que();            
        }
        valid_flag = SLC_nand_oob_read(S_SECTOR(blk,i*S_SECT_NUM_PER_PAGE));
        if(valid_flag == 1){
            valid_sect_num = SLC_nand_page_read(S_SECTOR(blk,i*S_SECT_NUM_PER_PAGE),copy_lsn,1);
            ASSERT(valid_sect_num == S_SECT_NUM_PER_PAGE);
            SLC_opagemap[S_BLK_PAGE_NO_SECT(copy_lsn[0])].ppn = S_BLK_PAGE_NO_SECT(S_SECTOR(free_cold_blk_no,free_cold_page_no));
            SLC_nand_page_write(S_SECTOR(free_cold_blk_no,free_cold_page_no)&(~S_OFF_MASK_SECT),copy_lsn,1,1);
            free_cold_page_no += S_SECT_NUM_PER_PAGE;
        }
    }
    victim_blkno=blk; 
    SLC_nand_erase(victim_blkno);   
}


/******************************************
* Name : SLC_Cold_Circular_Que
* Date : 2018-12-10 
* author: ymb 
* param: 
* return value: 
* Attention : 
***********************************************/
void Comb_SLC_Cold_Circular_Que()
{
    int SLC_blk_index;
    if(SLC_cold_head >= SLC_cold_tail){// equal only happen in cold init write
        if(SLC_cold_head - SLC_nand_blk <= SLC_ARR_LEN - COLD_FREE_MIN){
            SLC_cold_head++;
            free_cold_blk_no = SLC_nand_get_cold_free_blk(0);
            free_cold_page_no = 0;
        }else{
            SLC_blk_index = SLC_cold_tail - SLC_nand_blk;
            SLC_cold_head++;
            if(SLC_cold_head - SLC_nand_blk == SLC_ARR_LEN){
				SLC_cold_head = &SLC_nand_blk[SLC_HOT_ARR_LEN];
			}
            free_cold_blk_no = SLC_nand_get_cold_free_blk(0);
            free_cold_page_no = 0;
            Comb_SLC_data_move(SLC_blk_index);
            SLC_cold_tail++;
        }
    }
    else{  // head <  tail
        SLC_blk_index = SLC_cold_tail - SLC_nand_blk;
        SLC_cold_head++;
        free_cold_blk_no = SLC_nand_get_cold_free_blk(0);
        free_cold_page_no = 0;
        Comb_SLC_data_move(SLC_blk_index);
        SLC_cold_tail++;
        if(SLC_cold_tail - SLC_nand_blk == SLC_ARR_LEN)
            SLC_cold_tail = &SLC_nand_blk[SLC_HOT_ARR_LEN];
    }
}


/************************CFTL INTERFACE FUNCTION***********************/
/***************************************************
 * mapdir_flag : 0 SLC(data) ,1 MLC(data), 2 MLC(map)
 * ************************************************/
size_t Comb_read(sect_t lsn, sect_t size, int mapdir_flag)
{
   int sect_num ;
   if(mapdir_flag == 0){
		sect_num = Comb_SLC_opm_read(lsn, size ,mapdir_flag);
   }else if(mapdir_flag >= 1 && mapdir_flag <=2){
		sect_num = Comb_MLC_opm_read(lsn, size, mapdir_flag);
   }else{
	    printf("mapdir_flag must select(0,1,2)\n");
		assert(0);
   }
   return sect_num;
}

size_t Comb_write(sect_t lsn, sect_t size, int mapdir_flag)
{
	int sect_num;
	if(mapdir_flag == 0){
		//data write to SLC hot region
		sect_num = Comb_SLC_opm_write(lsn, size ,mapdir_flag);
	}else if(mapdir_flag >= 1 && mapdir_flag <=2){
		sect_num = Comb_MLC_opm_write(lsn, size, mapdir_flag);
	}else{
		printf("mapdir_flag must select(0,1,2)\n");
		assert(0);
	}
	return sect_num;
}

void Comb_end()
{
  if (SLC_opagemap != NULL) {
    free(SLC_opagemap);
    
  }
  if(MLC_mapdir != NULL){
	free(MLC_mapdir);
  }
  
  if(MLC_opagemap != NULL){
	free(MLC_opagemap);
  }
}

/******************************************************
 * Attention : SLC_blk_num = MLC_blk_num * SLC_ratio;
 * in order to map all lpn ,SLC and MLC all have same volume
 * ****************************************************/
int Comb_init(blk_t MLC_blk_num, blk_t extra_num )
{
  int i;
  int MLC_mapdir_num;
  int All_Volume_Sect = MLC_blk_num * M_SECT_NUM_PER_BLK * (1 + SLC_ratio);
  
  SLC_opagemap_num = All_Volume_Sect / S_SECT_NUM_PER_PAGE;
  if(All_Volume_Sect % S_SECT_NUM_PER_PAGE !=0){
	SLC_opagemap_num ++;
  }
  
  MLC_opagemap_num = All_Volume_Sect / M_SECT_NUM_PER_PAGE;
  if(All_Volume_Sect % M_SECT_NUM_PER_PAGE !=0){
	MLC_opagemap_num ++;
  }

  //create primary mapping table
  SLC_opagemap = (struct mix_opm_entry *) malloc(sizeof (struct mix_opm_entry) * SLC_opagemap_num);
  MLC_opagemap = (struct mix_opm_entry *) malloc(sizeof (struct mix_opm_entry) * MLC_opagemap_num);

  MLC_mapdir_num = MLC_opagemap_num / MLC_MAP_ENTRIES_PER_PAGE;
  if(MLC_opagemap_num % MLC_MAP_ENTRIES_PER_PAGE !=0){
	MLC_map_dir_num ++;
  }
  MLC_mapdir = (struct mix_omap_dir *)malloc(sizeof(struct mix_omap_dir) * MLC_mapdir_num );
  
  if ((SLC_opagemap == NULL) ||(MLC_opagemap == NULL) || (MLC_mapdir == NULL)) {
    return -1;
  }
  
  memset(SLC_opagemap, 0xFF, sizeof (struct mix_opm_entry) * SLC_opagemap_num);
  memset(MLC_opagemap, 0xFF, sizeof (struct mix_opm_entry) * MLC_opagemap_num);
  memset(MLC_mapdir,  0xFF, sizeof (struct mix_omap_dir) * MLC_mapdir_num);
  //youkim: 1st map table 
  SLC_TOTAL_MAP_ENTRIES = SLC_opagemap_num;
  MLC_TOTAL_MAP_ENTRIES = MLC_opagemap_num;

  for(i = 0; i<SLC_TOTAL_MAP_ENTRIES; i++){
    SLC_opagemap[i].cache_status = 0;
    SLC_opagemap[i].cache_age = 0;
    SLC_opagemap[i].map_status = 0;
    SLC_opagemap[i].map_age = 0;
    SLC_opagemap[i].update = 0;
    SLC_opagemap[i].free = 1;
    SLC_opagemap[i].ppn =-1;
    SLC_opagemap[i].count = 0;
  }
  for(i = 0; i<MLC_TOTAL_MAP_ENTRIES; i++){
    MLC_opagemap[i].cache_status = 0;
    MLC_opagemap[i].cache_age = 0;
    MLC_opagemap[i].map_status = 0;
    MLC_opagemap[i].map_age = 0;
    MLC_opagemap[i].update = 0;
    MLC_opagemap[i].free = 1;
    MLC_opagemap[i].ppn = -1;
  }
  extra_blk_num = extra_num;
  //wfk:获取第0个块作为SLC的data blk
  free_SLC_blk_no[1] = nand_get_SLC_free_blk(0);
  free_SLC_page_no[1] = 0;
  
  free_cold_blk_no = SLC_nand_get_cold_free_blk(0);
  free_cold_page_no = 0;
  
  //wfk:获取第4096个块作为MLC翻译块
  free_MLC_blk_no[0] = nand_get_MLC_free_blk(0);
  free_MLC_page_no[0] = 0;
  //wfk:获取第4097个块作为MLC的数据块
  free_MLC_blk_no[1] = nand_get_MLC_free_blk(0);
  free_MLC_page_no[1] = 0;
  //initialize variables
  SYNC_NUM = 0;
  
  
  SLC_write_count = 0;
  SLC_read_count = 0;
  MLC_write_count = 0;
  MLC_read_count = 0;
  //update 2nd mapping table  
  for(i = 0; i< MLC_mapdir_num ; i++){
    ASSERT(MLC_MAP_ENTRIES_PER_PAGE == 1024);
    Comb_MLC_opm_write(i*M_SECT_NUM_PER_PAGE, M_SECT_NUM_PER_PAGE, 2);
  }

  return 0;
}

/*********************************
 * 
 * Attention : this function to find cold blk in MLC and swap data
 *********************************/
void Comb_MLC_opm_wear_level(int target_blk_no)
{

	int merge_count;
	int i,z, j,m,map_flag,q;
	int k,old_flag,temp_arr[M_PAGE_NUM_PER_BLK],temp_arr1[M_PAGE_NUM_PER_BLK],map_arr[M_PAGE_NUM_PER_BLK]; 
	int valid_flag,pos;
	_u32 copy_lsn[M_SECT_NUM_PER_PAGE], copy[M_SECT_NUM_PER_PAGE];
	_u16 valid_sect_num,  l, s;
	_u32 old_ppn,new_ppn;
	sect_t s_lsn;	// starting logical sector number
    sect_t s_psn; // starting physical sector number 
    _u32 wear_src_blk_no,wear_target_blk_no;
    _u16 wear_src_page_no,wear_target_page_no;
    
	//	wear_src_blk_no=find_switch_cold_blk_method1(target_blk_no);
	wear_src_blk_no = MLC_find_switch_cold_blk_method2(target_blk_no);
	wear_target_blk_no = target_blk_no;
	// target-blk-no nand_blk state free must to be 0
	MLC_nand_blk[target_blk_no].state.free=0;
	free_MLC_blk_num--;
	
	wear_src_page_no=0;
	wear_target_page_no=0;

	memset(copy_lsn, 0xFF, sizeof (copy_lsn));
	
	map_flag= -1;
	for( q = 0; q < M_PAGE_NUM_PER_BLK; q++){
		if(MLC_nand_blk[wear_src_blk_no].page_status[q] == 1){ //map block
#ifdef WEARDEBUG
	    	printf("wear level block gcc select Map blk no: %d\n",wear_src_blk_no);
#endif
			for( q = 0; q  < M_PAGE_NUM_PER_BLK; q++) {
				if(MLC_nand_blk[wear_src_blk_no].page_status[q] == 0 ){
					printf("something corrupted1=%d",wear_src_blk_no);
        		}
      		}
      		map_flag = 0;
      		break;
    	} 
    	else if(MLC_nand_blk[wear_src_blk_no].page_status[q] == 0){ //data block
#ifdef WEARDEBUG
			printf("wear level block gcc select Data blk no: %d\n",wear_src_blk_no);
#endif
			for( q = 0; q  < M_PAGE_NUM_PER_BLK; q++) {
        		if(MLC_nand_blk[wear_src_blk_no].page_status[q] == 1 ){
          			printf("something corrupted2=%d",wear_src_blk_no);
        		}
      		}
      		map_flag = 1;
      		break;
    	}
  	}
	ASSERT ( map_flag== 0 || map_flag == 1);
	
 	pos = 0;
 	merge_count = 0;
 	for (i = 0; i < M_PAGE_NUM_PER_BLK; i++) 
 	{
		valid_flag = MLC_nand_oob_read( M_SECTOR(wear_src_blk_no, i * M_SECT_NUM_PER_PAGE));		
   		if(valid_flag == 1)
   		{
	   		valid_sect_num = MLC_nand_page_read(M_SECTOR(wear_src_blk_no, i * M_SECT_NUM_PER_PAGE), copy, 1);
	   		merge_count++;
	   		ASSERT(valid_sect_num == M_SECT_NUM_PER_PAGE);
	   		k=0;
	   		for (j = 0; j < valid_sect_num; j++) {
		 		copy_lsn[k] = copy[j];
		 		k++;
	   		}
	   		
		 	if(MLC_nand_blk[wear_src_blk_no].page_status[i] == 1)
		 	{	
				
		   		MLC_mapdir[(copy_lsn[0]/M_SECT_NUM_PER_PAGE)].ppn =  M_BLK_PAGE_NO_SECT(M_SECTOR(wear_target_blk_no, wear_target_page_no));
		   		MLC_opagemap[copy_lsn[0]/M_SECT_NUM_PER_PAGE].ppn =  M_BLK_PAGE_NO_SECT(M_SECTOR(wear_target_blk_no, wear_target_page_no));
		   		MLC_nand_page_write(M_SECTOR(wear_target_blk_no,wear_target_page_no) & ( ~ M_OFF_MASK_SECT), copy_lsn, 1, 2);
		   		wear_target_page_no += M_SECT_NUM_PER_PAGE;
		 	}else{
				old_ppn=MLC_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].ppn;
#ifdef WEARDEBUG
				if(old_ppn != MLC_BLK_PAGE_NO_SECT(M_SECTOR(wear_src_blk_no, i * M_SECT_NUM_PER_PAGE))){
					printf("debug :old ppn:%d\t MLC BLK_PAGE_NO_SECT:%d\n",old_ppn,M_BLK_PAGE_NO_SECT(M_SECTOR(wear_src_blk_no, i * M_SECT_NUM_PER_PAGE)));
				}
#endif
		   		MLC_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(wear_target_blk_no, wear_target_page_no));
				new_ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(wear_target_blk_no, wear_target_page_no));
		   		MLC_nand_page_write(M_SECTOR(wear_target_blk_no, wear_target_page_no) & (~M_OFF_MASK_SECT), copy_lsn, 1, 1);
		   		wear_target_page_no += M_SECT_NUM_PER_PAGE;
		   		if((MLC_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].map_status == MAP_REAL) || (MLC_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].map_status == MAP_GHOST)) {
			 		delay_flash_update++;
					MLC_nand_ppn_2_lpn_in_CMT_arr[old_ppn]=0;
					MLC_nand_ppn_2_lpn_in_CMT_arr[new_ppn]=1;
		   		}
		   		else {	
			 		map_arr[pos] = copy_lsn[0];
			 		pos++;
		   		} 
			}
   		}else{
			//unvalid page data
   			s_lsn = MLC_nand_blk[wear_src_blk_no].sect[i*M_SECT_NUM_PER_PAGE].lsn;
			for(k = 0 ; k < M_SECT_NUM_PER_PAGE ; k++){
				copy_lsn[k] = s_lsn+k ;
			}
			s_psn = M_SECTOR(wear_target_blk_no, wear_target_page_no) & (~M_OFF_MASK_SECT);	
			if(MLC_nand_blk[wear_src_blk_no].page_status[i] == 1){
				MLC_nand_page_write(M_SECTOR(wear_target_blk_no, wear_target_page_no) & (~M_OFF_MASK_SECT), copy_lsn, 1, 2);
			}else{
				MLC_nand_page_write(M_SECTOR(wear_target_blk_no, wear_target_page_no) & (~M_OFF_MASK_SECT), copy_lsn, 1, 1);
			}
			wear_target_page_no+= M_SECT_NUM_PER_PAGE;
			for(k = 0; k < M_SECT_NUM_PER_PAGE; k++){
      			MLC_nand_invalidate(s_psn + k, s_lsn + k);
    		} 
   		}
 	}//end-for
 #ifdef DEBUG
	if(MLC_nand_blk[wear_target_blk_no].fpc !=0 ){
		printf("nand blk %d is not write full\n",wear_target_blk_no);
	}
#endif 
	for(i=0;i < M_PAGE_NUM_PER_BLK;i++) {
		temp_arr[i]=-1;
	}
	k=0;
	for(i =0 ; i < pos; i++) {
		old_flag = 0;
		for( j = 0 ; j < k; j++) {		
			if(temp_arr[j] == MLC_mapdir[((map_arr[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn) {
				if(temp_arr[j] == -1){
					printf("something wrong");
					ASSERT(0);
				}
				old_flag = 1;
				break;
		 	}
		}
		if( old_flag == 0 ) {
		 	temp_arr[k] = MLC_mapdir[((map_arr[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn;
		 	temp_arr1[k] = map_arr[i];
		 	k++;
		}
	}//end-for
	for ( i=0; i < k; i++) {
		if (free_MLC_page_no[0] >= M_SECT_NUM_PER_BLK) {
			if((free_MLC_blk_no[0] = nand_get_MLC_free_blk(1)) == -1){
				printf("we are in big trouble shudnt happen");
			}
			free_MLC_page_no[0] = 0;
		}
		MLC_nand_page_read(temp_arr[i]*M_SECT_NUM_PER_PAGE,copy,1);

		for(m = 0; m < M_SECT_NUM_PER_PAGE; m++){
			MLC_nand_invalidate(MLC_mapdir[((temp_arr1[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn * M_SECT_NUM_PER_PAGE + m, copy[m]);
		} 
		nand_stat(OOB_WRITE);
		MLC_mapdir[((temp_arr1[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn  = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[0], free_MLC_page_no[0]));
		MLC_opagemap[((temp_arr1[i]/M_SECT_NUM_PER_PAGE)/MLC_MAP_ENTRIES_PER_PAGE)].ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[0], free_MLC_page_no[0]));
		MLC_nand_page_write(M_SECTOR(free_MLC_blk_no[0],free_MLC_page_no[0]) & (~M_OFF_MASK_SECT), copy, 1, 2);
		free_MLC_page_no[0] += M_SECT_NUM_PER_PAGE;
	}
	
   	if(merge_count == 0 ) 
    	merge_switch_num++;
  	else if(merge_count > 0 && merge_count < M_PAGE_NUM_PER_BLK)
    	merge_partial_num++;
  	else if(merge_count == M_PAGE_NUM_PER_BLK)
    	merge_full_num++;
  	else if(merge_count > M_PAGE_NUM_PER_BLK){
    	printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,M_PAGE_NUM_PER_BLK);
    	ASSERT(0);
  	}

	MLC_nand_erase(wear_src_blk_no);
	
}//end-func


struct ftl_operation Comb_operation = {
  init:  Comb_init,
  read:  Comb_read,
  write: Comb_write,
  end:   Comb_end
};
  
struct ftl_operation * Comb_setup()
{
  return & Comb_operation;
}

