/******************************************************
 * Author :zhoujie
 * date :09.12.2018
 * function: the interval layer(FTL) to simulation for HybridSSD
 * MLC take DFTL alogrithm ; SLC take pure PAGE FTL alogrithm 
 * **************************************************/
#include "HBFTL.h"
#include "ssd_interface.h"
#include "TwoMeans.h"
#include "flash.h"

#define MAP_INVALID 1 
#define MAP_REAL 2
#define MAP_GHOST 3

static int MIX_MAP_REAL_NUM_ENTRIES;
static int MIX_MAP_GHOST_NUM_ENTRIES;

static int MLC_page_num_for_2nd_map_table;
static int operation_time;
static int update_reqd;

static int real_min = -1;
static int real_max = 0;

static double RWs,RWm;
static int Wear_Th;
#define MAX_TH 64
#define MIN_TH 4
static int last_SLC_to_MLC_Counts;
static double Comb_Tau = 0.1;
static double Comb_Tau_std = 0.05;

#define CFTL_CYCLE_COUNTS 1000
#define COMB_CYCLE_COUNTS (nand_SLC_blk_num * S_SECT_NUM_PER_BLK)
int CFTL_Window[CFTL_CYCLE_COUNTS];
int Curr_Count;

#define MAP_REAL_MAX_ENTRIES 6552		// real map table size in bytes
#define MAP_GHOST_MAX_ENTRIES 1640		// ghost_num is no of entries chk if this is ok
static int ghost_arr[MAP_GHOST_MAX_ENTRIES]; // -1 is unvalid value
static int real_arr[MAP_REAL_MAX_ENTRIES]; // -1 is unvalid value


#ifdef DEBUG
int debug_count = 0;
#endif
int init_flag = 0;
/**********Function  Declare**********/
static void mix_find_real_min();
static int mix_find_min_ghost_entry();
static int search_table(int *arr, int size, int val);
static void mix_find_real_max();
static int find_free_pos( int *arr, int size);
/***************************************/
void HBFTL_init();
static void Wear_Th_update(int scount);
double HBFTL_Scheme(unsigned int secno,int scount,int operation);

void read_from_mix_flash(unsigned int secno, int scount, int operation);
int read_from_SLC(int blkno,int read_num);
void read_from_MLC(int blkno);

void HBFTL_MLC_Hit_CMT(int blkno,int operation);
void HBFTL_MLC_No_Hit_CMT(int blkno,int operation);
void Write_2_SLC(unsigned int secno, int scount);
void Write_2_MLC(unsigned int secno,int scount);
int Check_blkno_in_MLC(int blkno);
int Check_blkno_in_SLC(int blkno);

static int MLC_opm_invalid(int lpn);
static int SLC_opm_invalid(int lpn);
/***********************************************
 *  some 	inner function
 * Date :10.12.2018 
 * ********************************************/
#ifdef DEBUG
int compute_real_arr_size()
{
	int len =0,i;
	for(i=0; i <MAP_REAL_MAX_ENTRIES;i++ ){
		if(real_arr[i] != -1){
			len ++;
		}
	}
	return len;
}

int compute_ghost_arr_size()
{
	int len =0,i;
	for(i=0; i <MAP_GHOST_MAX_ENTRIES;i++ ){
		if(ghost_arr[i] != -1){
			len ++;
		}
	}
	return len;
}
#endif


static void mix_find_real_min()
{
  int i,index; 
  int temp = 99999999;
  
  for(i=0; i < MAP_REAL_MAX_ENTRIES; i++) {
	  if(real_arr[i] >=0 ){
        if(MLC_opagemap[real_arr[i]].map_age <= temp) {
            real_min = real_arr[i];
            temp = MLC_opagemap[real_arr[i]].map_age;
            index = i;
        }
	 }
  }
      
}

static int mix_find_min_ghost_entry()
{
  int i; 

  int ghost_min = 0;
  int temp = 99999999; 
  for(i=0; i < MAP_GHOST_MAX_ENTRIES; i++) {
	if(ghost_arr[i] >= 0){
		if( MLC_opagemap[ghost_arr[i]].map_age <= temp) {
			ghost_min = ghost_arr[i];
			temp = MLC_opagemap[ghost_arr[i]].map_age;
		}
	}
  }
  return ghost_min;
}


static int search_table(int *arr, int size, int val) 
{
    int i;
    for(i =0 ; i < size; i++) {
        if(arr[i] == val) {
            return i;
        }
    }

    printf("shouldnt come here for search_table()=%d,%d",val,size);
    assert(0);
    for( i = 0; i < size; i++) {
      if(arr[i] != -1) {
        printf("arr[%d]=%d ",i,arr[i]);
      }
    }

    return -1;
}

static void mix_find_real_max()
{
  int i; 

  for(i=0;i < MAP_REAL_MAX_ENTRIES; i++) {
	  if(real_arr[i] >=0){
		if(MLC_opagemap[real_arr[i]].map_age > MLC_opagemap[real_max].map_age) {
			real_max = real_arr[i];
		}
	  }
  }
}

static int find_free_pos( int *arr, int size)
{
    int i;
    for(i = 0 ; i < size; i++) {
        if(arr[i] == -1) {
            return i;
        }
    } 
    printf("shouldnt come here for find_free_pos()");
    assert(0);
    return -1;
}
/*********************************************************/

void HBFTL_init()
{
	MLC_page_num_for_2nd_map_table = MLC_opagemap_num / MLC_MAP_ENTRIES_PER_PAGE;
	if(MLC_opagemap_num % MLC_MAP_ENTRIES_PER_PAGE != 0){
		MLC_page_num_for_2nd_map_table ++ ;
	}
	Curr_Count = 0;
	last_SLC_to_MLC_Counts = 0;
	memset(CFTL_Window , 0 , sizeof(int) * CFTL_CYCLE_COUNTS);
	
	memset(ghost_arr, -1, sizeof(int) * MAP_GHOST_MAX_ENTRIES);
	memset(real_arr, -1, sizeof(int) * MAP_REAL_MAX_ENTRIES);
	update_reqd = 0;

	MIX_MAP_REAL_NUM_ENTRIES = 0;
	MIX_MAP_GHOST_NUM_ENTRIES = 0;
	
	Wear_Th = 12;
	operation_time = 0;
    init_flag = 1;
}

static void Wear_Th_update(int scount)
{
   double Temp_Comb_Tau = 0.0;
   int i, CFTL_Sum = 0;
   RWs = SLC_stat_erase_num *1.0 / total_SLC_blk_num;
   RWm = MLC_stat_erase_num *1.0/ total_MLC_blk_num;
#ifdef DEBUG
	debug_count ++ ;
	if(debug_count % 1000 == 0){
		printf("SLC-Eve_ECN %lf\tMLC-Eve-ECN %lf\n",RWs,RWm);
		printf("Curr -Th is %d\n",Wear_Th);
		debug_count = 1 ;
	}
#endif
   if(ftl_type == 6){
	  // CFTL Th update
	  if(Curr_Count == CFTL_CYCLE_COUNTS){
		Wear_Th = TwoMeans(CFTL_Window, CFTL_CYCLE_COUNTS);
		Curr_Count = 0; 
		memset(CFTL_Window, 0, sizeof(int) * CFTL_CYCLE_COUNTS);
	  }
	  CFTL_Window[Curr_Count] = scount;
	  Curr_Count ++;
	  
   }else if(ftl_type == 7){
	 // CombFTL Th update
	 Curr_Count += scount;
	 if(Curr_Count >= COMB_CYCLE_COUNTS){
		Curr_Count = 0;
		Temp_Comb_Tau = ((SLC_to_MLC_counts-last_SLC_to_MLC_Counts) * 1.0 * M_SECT_NUM_PER_PAGE)/COMB_CYCLE_COUNTS;
		last_SLC_to_MLC_Counts = SLC_to_MLC_counts;
		if(Temp_Comb_Tau >= (Comb_Tau + Comb_Tau_std)){
			Wear_Th -= 2;
			Wear_Th = (Wear_Th < MIN_TH) ? MIN_TH : Wear_Th;
		}else if(Temp_Comb_Tau <=(Comb_Tau -Comb_Tau_std)){
			Wear_Th += 2;
			Wear_Th = (Wear_Th > MAX_TH) ? MAX_TH : Wear_Th;
		}else{
			printf("Temp_Comb_Tau is %lf\n",Temp_Comb_Tau);
		}
	}
   }
   
}


double HBFTL_Scheme(unsigned int secno,int scount,int operation)
{
	double delay = 0.0;
	int page_align = 0;
	//handle for warm done
	if(warm_flag == 0){
		Curr_Count = 0;
		last_SLC_to_MLC_Counts = 0;
		memset(CFTL_Window , 0 , sizeof(int) * CFTL_CYCLE_COUNTS);
		Wear_Th = 12;
		warm_flag = -1;
	}
	if(init_flag == 0){
		HBFTL_init();
	}
	
	if(operation == DATA_WRITE){
		real_data_write_sect_num += scount;
		//based on 4K(this problem need to slove)
		page_align = (secno + scount -1)/M_SECT_NUM_PER_PAGE - (secno)/M_SECT_NUM_PER_PAGE + 1;
		page_align_padding_sect_num +=(page_align * M_SECT_NUM_PER_PAGE -scount);
		Wear_Th_update(scount);
		
		if(scount >= Wear_Th){
			// write to MLC
			Write_2_MLC(secno, scount);
		}else{
			// write to SLC
			Write_2_SLC(secno, scount);
		}	
		
	}else{
		//check data storge in where
		read_from_mix_flash(secno,scount,operation);
	}
	delay = calculate_delay_MLC_flash() + calculate_delay_SLC_flash();
	return delay;
}


/***********************************************************
 * Name :read_from_mix_flash
 * date :10.12.2018
 * attention : data maybe alignment in 2K or 4K
 * *********************************************************/
void read_from_mix_flash(unsigned int secno, int scount, int operation)
{
	int bcount_4K,blkno_2K,blkno_4K,cnt;
	
	int Is_in_SLC,Is_in_MLC; // 1: MLC 2: SLC
	
	blkno_4K = secno/M_SECT_NUM_PER_PAGE + MLC_page_num_for_2nd_map_table;
	//attention compute page aligin
	blkno_2K = (secno/M_SECT_NUM_PER_PAGE)*(M_SECT_NUM_PER_PAGE/S_SECT_NUM_PER_PAGE);
	bcount_4K =  (secno + scount -1)/M_SECT_NUM_PER_PAGE - (secno)/M_SECT_NUM_PER_PAGE + 1;
	cnt = bcount_4K;
#ifdef DEBUG

#endif
	while(cnt > 0){
		cnt--;
		operation_time ++;
		Is_in_SLC = Is_in_MLC = 0;
		if(SLC_opagemap[blkno_2K].free == 0 ){
			Is_in_SLC = 1; 
		}
		if(MLC_opagemap[blkno_4K].free == 0 ){
			Is_in_MLC = 1;
		}
#ifdef DEBUG
		if(Is_in_SLC && Is_in_MLC){
			printf("MLC and SLC all have data,check operation_time\n");
			assert(0);
			if(SLC_opagemap[blkno_2K].map_age > MLC_opagemap[blkno_4K].map_age){
				//must clean MLC state flag
				Is_in_MLC = 0;
			}else{
				// must clean SLC state flag
				Is_in_SLC = 0;
			}
		}
#endif
		if(Is_in_SLC != 0){
			read_from_SLC(blkno_2K,2);
		}else if(Is_in_MLC !=0){
			read_from_MLC(blkno_4K);
		}else{
			printf("No page in Mix Flash!!\n");
			assert(0);
		}
		
		blkno_4K++;
		blkno_2K += 2;
	}
}

/***************************************
 * Name : read_from_SLC
 * Date : 10.12.2018
 * Author: zhoujie
 * Attention : SLC page based on 2K
 * param: read_num(1:read 1 page(2K),2:read 2 page(4K))
 * but operation_time counts based on 4K
 * and SLC data maybe not align in 4K
 **************************************/
int read_from_SLC(int blkno,int read_num)
{
	int cnt;
	if(read_num > 1 && SLC_opagemap[(blkno+read_num-1)].free == 1){
#ifdef DEBUG
		printf("SLC not storgae lpn %d\n",blkno+read_num-1);
#endif
		read_num = 1;
	}
	cnt = read_num;
	while(read_num >0){
		read_num --;
		//mapdir_flag = 0 --> operation SLC
		send_flash_request(blkno * S_SECT_NUM_PER_PAGE,S_SECT_NUM_PER_PAGE,1,0);
		SLC_opagemap[blkno].map_age = operation_time;
		blkno ++;
	}
	return cnt;
}

/***************************************
 * Name : read_from_MLC
 * Date :10.12.2018
 * Author: zhoujie
 * param : blkno -- based on 4K
 * return value : void
 **************************************/
void read_from_MLC(int blkno)
{
	if(MLC_opagemap[blkno].map_status == MAP_REAL || MLC_opagemap[blkno].map_status == MAP_GHOST){
		// hit cmt
		HBFTL_MLC_Hit_CMT(blkno,1);
	}else{
		// not-hit-cmt
		HBFTL_MLC_No_Hit_CMT(blkno,1);
	}
	//load data
	//mapdir_flag = 1 --> operation MLC data region
	MLC_read_count++;
	send_flash_request( blkno * M_SECT_NUM_PER_PAGE, M_SECT_NUM_PER_PAGE, 1, 1);
}

/**************************************************
 * Attention : Write to SLC ,MLC enrty should change
 * ***********************************************/

/****************************
* Name: HBFTL_MLC_Hit_CMT
* Date: 10.12.2018
* Author: zhoujie
* param: pageno(hit logical page number(lpn))
*		 operation(1:read ,0:write)
* return value:void
* Function:
* Attention:
*******************************/
void HBFTL_MLC_Hit_CMT(int blkno,int operation)
{
	
	int cnt,z; int min_ghost;	
	int pos=-1,pos_real=-1,pos_ghost=-1;
	
	MLC_opagemap[blkno].map_age = operation_time;
	
	if(MLC_opagemap[blkno].map_status == MAP_GHOST){
		//Hit Map_Ghost	maybe move to real list
		if ( real_min == -1 ) {
			real_min = 0;
			mix_find_real_min();
		}    
		if(MLC_opagemap[real_min].map_age <= MLC_opagemap[blkno].map_age) {
			mix_find_real_min();  // probably the blkno is the new real_min alwaz
			MLC_opagemap[blkno].map_status = MAP_REAL;
			MLC_opagemap[real_min].map_status = MAP_GHOST;
			pos_ghost = search_table(ghost_arr,MAP_GHOST_MAX_ENTRIES,blkno);
			ASSERT(pos_ghost != -1);
			ghost_arr[pos_ghost] = -1;
			pos_real = search_table(real_arr,MAP_REAL_MAX_ENTRIES,real_min);
			ASSERT(pos_real != -1);
			real_arr[pos_real] = -1;
			real_arr[pos_real]	 = blkno; 
			ghost_arr[pos_ghost] = real_min; 
		}
	}else if(MLC_opagemap[blkno].map_status == MAP_REAL) {
	// Hit Map_Real 
		if ( real_max == -1 ) {
			real_max = 0;
			mix_find_real_max();
			printf("Never happend\n");
		}

		if(MLC_opagemap[real_max].map_age <= MLC_opagemap[blkno].map_age){
			real_max = blkno;
		}  
	}else {
		printf("forbidden/shouldnt happen real =%d , ghost =%d\n",MAP_REAL,MAP_GHOST);
		ASSERT(0);
	}
}

/****************************
* Name: HBFTL_MLC_No_Hit_CMT
* Date: 10.12.2018
* Author: zhoujie
* param: pageno(hit logical page number(lpn))
*		 operation(1:read ,0:write)
* return value:void
* Function:
* Attention:
*******************************/
void HBFTL_MLC_No_Hit_CMT(int blkno,int operation)
{
	int min_ghost;	
	int pos=-1,pos_real=-1,pos_ghost=-1;

	//real-CMT is full
	if((MAP_REAL_MAX_ENTRIES - MIX_MAP_REAL_NUM_ENTRIES) == 0){
		//ghost-CMT is full
		if((MAP_GHOST_MAX_ENTRIES - MIX_MAP_GHOST_NUM_ENTRIES) == 0){
			//rm ghost-CMT-entry
			min_ghost = mix_find_min_ghost_entry();
			if(MLC_opagemap[min_ghost].update == 1){
				update_reqd ++;
				MLC_opagemap[min_ghost].update = 0;
				// read from 2nd mapping table then update it
				send_flash_request(((min_ghost-MLC_page_num_for_2nd_map_table)/MLC_MAP_ENTRIES_PER_PAGE) * M_SECT_NUM_PER_PAGE, M_SECT_NUM_PER_PAGE, 1, 2);   
				// write into 2nd mapping table 
				send_flash_request(((min_ghost-MLC_page_num_for_2nd_map_table)/MLC_MAP_ENTRIES_PER_PAGE) * M_SECT_NUM_PER_PAGE, M_SECT_NUM_PER_PAGE, 0, 2);   
			}
			MLC_opagemap[min_ghost].map_status = MAP_INVALID;
			
			mix_find_real_min();
			MLC_opagemap[real_min].map_status = MAP_GHOST;
			pos = search_table(ghost_arr, MAP_GHOST_MAX_ENTRIES,min_ghost);
			ASSERT(pos != -1);
			ghost_arr[pos] = -1;
			ghost_arr[pos] = real_min;
			pos = search_table(real_arr, MAP_REAL_MAX_ENTRIES,real_min);
			real_arr[pos] = -1;
			MIX_MAP_REAL_NUM_ENTRIES --;
		}else{
			// ghost-list not full
			mix_find_real_min();
			MLC_opagemap[real_min].map_status = MAP_GHOST;
			pos = search_table(real_arr,MAP_REAL_MAX_ENTRIES,real_min);
			ASSERT(pos != -1);
			real_arr[pos] = -1;
			MIX_MAP_REAL_NUM_ENTRIES --;
			pos = find_free_pos(ghost_arr,MAP_GHOST_MAX_ENTRIES);
			ASSERT(pos !=-1);
			ghost_arr[pos] = real_min;
			MIX_MAP_GHOST_NUM_ENTRIES++;
		}
	}
	// read from 2nd mapping table load into CMT
	send_flash_request( ((blkno - MLC_page_num_for_2nd_map_table)/MLC_MAP_ENTRIES_PER_PAGE * M_SECT_NUM_PER_PAGE), M_SECT_NUM_PER_PAGE, 1, 2);
	MLC_opagemap[blkno].map_status = MAP_REAL;
	MLC_opagemap[blkno].map_age = operation_time;
	real_max = blkno;
	MIX_MAP_REAL_NUM_ENTRIES ++;			
	pos = find_free_pos(real_arr,MAP_REAL_MAX_ENTRIES);
	real_arr[pos] = blkno;	
}


/*********************************
 * Name : Write_2_SLC
 * Date : 10.12.2018
 * Author : Zhoujie
 * param : secno(sector size),scount(sector size)
 * return value : void
 * Attention: SLC page size based on 2K ,MLC lpn(based on 4K) + map_lpn_addres
 *********************************/
void Write_2_SLC(unsigned int secno, int scount)
{
    int bcount_4K,blkno_2K,blkno_4K,cnt;
	
	blkno_4K = secno/M_SECT_NUM_PER_PAGE + MLC_page_num_for_2nd_map_table;
	//attention compute 
	blkno_2K = (secno/M_SECT_NUM_PER_PAGE)*(M_SECT_NUM_PER_PAGE/S_SECT_NUM_PER_PAGE);
	bcount_4K =  (secno + scount -1)/M_SECT_NUM_PER_PAGE - (secno)/M_SECT_NUM_PER_PAGE + 1;
	//curr debug force 4K need to slove
	cnt = bcount_4K;
	while(cnt > 0){
		cnt--;
		operation_time ++;
		//check MLC page is storge 
		Check_blkno_in_MLC(blkno_4K);
		// mapdir_flag = 0 --> SLC
		send_flash_request(blkno_2K * S_SECT_NUM_PER_PAGE ,S_SECT_NUM_PER_PAGE,0,0);
		send_flash_request((blkno_2K+1) * S_SECT_NUM_PER_PAGE, S_SECT_NUM_PER_PAGE,0,0);
		blkno_4K ++;
		blkno_2K += 2;
	}
}

/*********************************
 * Name : Write_2_MLC
 * Date : 10.12.2018
 * Author : Zhoujie
 * param : secno(sector size),scount(sector size)
 * return value : void
 * Attention: MLC page size based on 4K
 *********************************/
void Write_2_MLC(unsigned int secno,int scount)
{    
#ifdef DEBUG
	int ppn,debug_ppn,debug_scn,debug_blk;
#endif
	int bcount_4K,blkno_2K,blkno_4K,cnt;
	
	blkno_4K = secno/M_SECT_NUM_PER_PAGE + MLC_page_num_for_2nd_map_table;
	//attention page align
	blkno_2K = (secno/M_SECT_NUM_PER_PAGE)*(M_SECT_NUM_PER_PAGE/S_SECT_NUM_PER_PAGE);
	bcount_4K =  (secno + scount -1)/M_SECT_NUM_PER_PAGE - (secno)/M_SECT_NUM_PER_PAGE + 1;
	//curr debug force 4K need to slove
	cnt = bcount_4K;
	while(cnt >0){
		cnt -- ;
		operation_time ++;
		Check_blkno_in_SLC(blkno_2K);
		Check_blkno_in_SLC((blkno_2K+1));
		if(MLC_opagemap[blkno_4K].map_status == MAP_REAL || MLC_opagemap[blkno_4K].map_status == MAP_GHOST){
			HBFTL_MLC_Hit_CMT(blkno_4K,0);
//#ifdef DEBUG
			//ASSERT(MIX_MAP_GHOST_NUM_ENTRIES == compute_ghost_arr_size());
			//ASSERT(MIX_MAP_REAL_NUM_ENTRIES == compute_real_arr_size());
//#endif
		}else{
			HBFTL_MLC_No_Hit_CMT(blkno_4K,0);
//#ifdef DEBUG
			//ASSERT(MIX_MAP_GHOST_NUM_ENTRIES == compute_ghost_arr_size());
			//ASSERT(MIX_MAP_REAL_NUM_ENTRIES == compute_real_arr_size());
//#endif	
		}
		//mapdir-flag 1 --> MLC data
		send_flash_request(blkno_4K * M_SECT_NUM_PER_PAGE, M_SECT_NUM_PER_PAGE, 0 ,1);
#ifdef DEBUG
		ppn = MLC_opagemap[blkno_4K].ppn;
		debug_blk = ppn >> 7;
		debug_ppn = ppn % 128;
		debug_scn = debug_ppn * 8;
		ASSERT(MLC_nand_blk[debug_blk].sect[debug_scn].lsn == (blkno_4K * 8));
#endif
		MLC_opagemap[blkno_4K].update = 1;
		blkno_2K += 2;
		blkno_4K ++;
	}
}

/************************************************
 * Name :  Check_blkno_in_MLC
 * Date : 10.12.2018
 * Author :ZhouJie
 * param : 
 * return value : flag( 1: inMLC , 0 : Not in MLC)
 * attention : MLC lpn(based on 4K) + map_lpn_addres
 * *********************************************/
int Check_blkno_in_MLC(int blkno)
{
#ifdef DEBUG
	int debug_ppn,debug_scn,debug_blk;
#endif
	int flag = 0;
	int pos= -1;
	if( MLC_opagemap[blkno].free == 0){
		flag = 1;
		if(MLC_opagemap[blkno].map_status == MAP_REAL){
#ifdef DEBUG
			debug_ppn = MLC_opagemap[blkno].ppn;
			debug_blk = debug_ppn >> 7;
			debug_scn = (debug_ppn % 128)*8;
			ASSERT(MLC_nand_blk[debug_blk].sect[debug_scn].lsn == (blkno*8));
#endif
			pos = search_table(real_arr, MAP_REAL_MAX_ENTRIES, blkno);
			ASSERT(pos !=-1 );
			real_arr[pos] = -1;
			MIX_MAP_REAL_NUM_ENTRIES -- ;		
		}else if(MLC_opagemap[blkno].map_status == MAP_GHOST){
#ifdef DEBUG
			debug_ppn = MLC_opagemap[blkno].ppn;
			debug_blk = debug_ppn >> 7;
			debug_scn = (debug_ppn % 128)*8;
			ASSERT(MLC_nand_blk[debug_blk].sect[debug_scn].lsn == (blkno*8));
#endif
			pos = search_table(ghost_arr, MAP_GHOST_MAX_ENTRIES,blkno);
			ASSERT(pos !=-1 );
			ghost_arr[pos] = -1;
			MIX_MAP_GHOST_NUM_ENTRIES --;
		}
		//invalid MLC page data and reset MLC_opagemap state
		MLC_opm_invalid(blkno);
	}
	return flag ;
}


/************************************************
 * Name :  Check_blkno_in_SLC
 * Date : 10.12.2018
 * Author :ZhouJie
 * param : 
 * return value : flag( 1: inSLC , 0 : Not in SLC)
 * attention : 
 * *********************************************/
int Check_blkno_in_SLC(int blkno)
{
	int flag = 0;
	if(SLC_opagemap[blkno].free == 0 ){
		flag = 1;
		SLC_opm_invalid(blkno);
	}
	return flag ;
}



/*****************************************
 * Name :MLC_opm_invalid
 * Date :10.12.2018 
 * Author : zhoujie
 * param : lpn (page based on 4K)
 * return value : INVALID PAGE SIZE
 * attention : MLC lpn(based on 4K) + map_lpn_addres
 ******************************************/
static int MLC_opm_invalid(int lpn)
{
  
  int s_lsn = lpn * M_SECT_NUM_PER_PAGE;
  int i, s_psn1;

  ASSERT(MLC_opagemap[lpn].ppn != -1);
  s_psn1 = MLC_opagemap[lpn].ppn * M_SECT_NUM_PER_PAGE;
  for(i = 0; i < M_SECT_NUM_PER_PAGE; i++){
      MLC_nand_invalidate(s_psn1 + i, s_lsn + i);
  }
  MLC_opagemap[lpn].ppn = -1;
  MLC_opagemap[lpn].map_status = 0;
  MLC_opagemap[lpn].map_age = 0;
  MLC_opagemap[lpn].update = 0;
  MLC_opagemap[lpn].free = 1;
  MLC_opagemap[lpn].count = 0;
  return M_SECT_NUM_PER_PAGE;
}

/*****************************************
 * Name :SLC_opm_invalid
 * Date :10.12.2018 
 * Author : zhoujie
 * param : lpn (page based on 2K)
 * return value : INVALID PAGE SIZE
 * attention : 
 ******************************************/
static int SLC_opm_invalid(int lpn)
{
  
  int s_lsn = lpn * S_SECT_NUM_PER_PAGE;
  int i, s_psn1;

  ASSERT(SLC_opagemap[lpn].ppn != -1);
  s_psn1 = SLC_opagemap[lpn].ppn * S_SECT_NUM_PER_PAGE;
  for(i = 0; i < S_SECT_NUM_PER_PAGE; i++){
      SLC_nand_invalidate(s_psn1 + i, s_lsn + i);
  }
  SLC_opagemap[lpn].ppn = -1;
  SLC_opagemap[lpn].map_status = 0;
  SLC_opagemap[lpn].map_age = 0;
  SLC_opagemap[lpn].update = 0;
  SLC_opagemap[lpn].free = 1;
  SLC_opagemap[lpn].count = 0;
  
  return S_SECT_NUM_PER_PAGE;
}

