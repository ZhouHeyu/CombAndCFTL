#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<math.h>

#define MAX_SECTOR_EXP 11
 typedef struct Means_cluster{
	int Cluster_num;// cluster center num
	int Msm;// cluster group msm num
	int group_arr_exp[MAX_SECTOR_EXP];//first need to set 0
 }Cluster;
 static Cluster Ci,Cj;
/********************************/
void Compute_arr_MSEM(Cluster * C)
{
	int i,msm = 0;
	int temp,center_num;
	center_num = (*C).Cluster_num;
	for(i = 0 ; i < MAX_SECTOR_EXP ; i++){
		if((*C).group_arr_exp[i] > 0){
			temp = 1 << i;
			msm += abs(center_num - temp) * (*C).group_arr_exp[i];
		}
	}
	(*C).Msm = msm;
}

void Reset_Cluster_Center(Cluster *C)
{
	int sum = 0,i,member_num = 0;
	int center_num = 0;
	int large_limit,less_limit,temp;
	for(i = 0 ;i <MAX_SECTOR_EXP ; i++){
		temp = 1 << i;
		sum += temp * (*C).group_arr_exp[i];
		member_num += (*C).group_arr_exp[i];
	}
	center_num = (int)(sum * 1.0/ member_num + 0.5);
	
	for(i = 0; i < MAX_SECTOR_EXP ; i++){
		large_limit = 1 << i;
		if(center_num <= large_limit){
			break;
		}
	}
	(*C).Cluster_num = large_limit;
	//if(i > 0){
		//less_limit = 1 << (i-1);
	//}                                                        
	//if( (center_num - less_limit) < (large_limit - center_num)){
		//(*C).Cluster_num = less_limit;
	//}else{
		//(*C).Cluster_num = large_limit;
	//}
}

int Translate_to_2(int x)
{
	int i ,temp,res;
	assert(x >= 1);
	for(i = 0 ; i < MAX_SECTOR_EXP ; i++){
		temp = 1 << i;
		if(temp > x){
			assert(i > 0);
			res = (i-1);
			break;
		}
	}
	return res;
}

/**************************************
 * Name :Compute_distance
 * return value : 1 Ci 2 Cj(x belong to center)
 * **************************/
int Compute_distance(int x ,int Ci, int Cj)
{
	if(abs(x - Ci) < abs(x -Cj)){
		return 1;
	}else{
		return 2;
	}
}



void data_allocate(int * arr ,int size)
{
	int i,j,flag;
	int exp_index;
	memset(Cj.group_arr_exp, 0 , sizeof(int) * MAX_SECTOR_EXP);
	memset(Ci.group_arr_exp, 0 , sizeof(int) * MAX_SECTOR_EXP);
	for( i = 0; i < size; i++){
		flag = Compute_distance(arr[i],Ci.Cluster_num,Cj.Cluster_num);
		exp_index = Translate_to_2(arr[i]);
		if(flag == 1){
			Ci.group_arr_exp[exp_index] ++;
		}else{
			Cj.group_arr_exp[exp_index] ++;
		}
	}
}

/*********************************************************
 * Name : TwoMeans
 * 
 * 
 * ******************************************************/
int TwoMeans(int *arr,int size)
{
	int min_i;
	int last_MSEM ,count;
	memset(Cj.group_arr_exp, 0 , sizeof(int) * MAX_SECTOR_EXP);
	memset(Ci.group_arr_exp, 0 , sizeof(int) * MAX_SECTOR_EXP);
	Ci.Cluster_num = 4;
	Cj.Cluster_num = 64;
	data_allocate(arr,size);
	Compute_arr_MSEM(&Ci);
	Compute_arr_MSEM(&Cj);
	Reset_Cluster_Center(&Ci);
	Reset_Cluster_Center(&Cj);
	count = 1;
	do{
		last_MSEM = Ci.Msm + Cj.Msm;
		printf("--------------iter-count %d-------------------------\n",count);
		printf("Curr-Ci-Center-num is %d\t Curr-Cj-Center-num is  %d\n",Ci.Cluster_num,Cj.Cluster_num);
		printf("Curr-MSM is %d\n",last_MSEM);
		data_allocate(arr,size);
		Compute_arr_MSEM(&Ci);
		Compute_arr_MSEM(&Cj);
		Reset_Cluster_Center(&Ci);
		Reset_Cluster_Center(&Cj);
		count ++;
	}while(last_MSEM > (Ci.Msm + Cj.Msm));
	printf("run finish\n");
	min_i = Ci.Cluster_num;
	return min_i;
}


/****************Test *************************************/
//~ int main()
//~ {
	//~ int last_MSEM ,count;
	//~ int T[100] = {1,15,8,10,1000,1024,10,1000,64,900,500,24,100,83,19,123,67,10};
	//~ memset(Cj.group_arr_exp, 0 , sizeof(int) * MAX_SECTOR_EXP);
	//~ memset(Ci.group_arr_exp, 0 , sizeof(int) * MAX_SECTOR_EXP);
	//~ Ci.Cluster_num = 4;
	//~ Cj.Cluster_num = 64;
	//~ data_allocate(T,10);
	//~ Compute_arr_MSEM(&Ci);
	//~ Compute_arr_MSEM(&Cj);
	//~ Reset_Cluster_Center(&Ci);
	//~ Reset_Cluster_Center(&Cj);
	//~ count = 1;
	//~ do{
		//~ last_MSEM = Ci.Msm + Cj.Msm;
		//~ printf("--------------iter-count %d-------------------------\n",count);
		//~ printf("Curr-Ci-Center-num is %d\t Curr-Cj-Center-num is  %d\n",Ci.Cluster_num,Cj.Cluster_num);
		//~ printf("Curr-MSM is %d\n",last_MSEM);
		//~ data_allocate(T,10);
		//~ Compute_arr_MSEM(&Ci);
		//~ Compute_arr_MSEM(&Cj);
		//~ Reset_Cluster_Center(&Ci);
		//~ Reset_Cluster_Center(&Cj);
		//~ count ++;
	//~ }while(last_MSEM > (Ci.Msm + Cj.Msm));
	//~ printf("run finish\n");
	//~ return 0;
//~ }
