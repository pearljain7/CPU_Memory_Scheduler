#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

int first_iter = 0;
int numDomains = 0;

struct domMemStats{
	virDomainPtr dptr;
	unsigned long long unusedMemory; //In kb
	unsigned long long availableMemory; //In kb
	unsigned long maxMemory; //In kb
};

unsigned long long hostMemFree=0;

struct domMemStats* dom_memstats;
unsigned long long* dom_memstats_prev;  //Previous iteration values of unused memory

void MemoryScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

int getMemStatsDomain(){
	unsigned int nr_stats = VIR_DOMAIN_MEMORY_STAT_NR;
	// printf("%d\n",nr_stats);
	virDomainMemoryStatPtr stats = (virDomainMemoryStatPtr)malloc(nr_stats*sizeof(virDomainMemoryStats));
	for(int i=0;i<numDomains;i++){
		if(virDomainMemoryStats(dom_memstats[i].dptr,stats,nr_stats,0)==-1){
			printf("Could not retrieve statistics\n");
		}
		for(int j=0;j<nr_stats;j++){
			if(stats[j].tag==VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
				dom_memstats[i].availableMemory = stats[j].val/1024;
			}
			else if(stats[j].tag==VIR_DOMAIN_MEMORY_STAT_UNUSED){
				dom_memstats[i].unusedMemory = stats[j].val/1024;
			}
			// else if(stats[j].tag==VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON){
			// 	dom_memstats[i].unusedMemory += stats[j].val/1024;
			// }
		}
	}
	return 0;
}

int getMemStatsHost(virConnectPtr conn){
	int nparams=0;
	if (virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, NULL, &nparams, 0) == 0 &&
    nparams != 0) {
		virNodeMemoryStats* params = malloc(sizeof(virNodeMemoryStats) * nparams);
		memset(params, 0, sizeof(virNodeMemoryStats) * nparams);
		if (virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, 0)==-1){
			printf("Could not retrieve host memory statistics\n");
		}
		for(int i=0;i<nparams;i++){
			if(strcmp(params[i].field,VIR_NODE_MEMORY_STATS_FREE)==0){
				hostMemFree = params[i].value/1024; //convert to MB
			}
		}
	}
	return 0;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	if(first_iter==0){
		//run init function once
		//populate vcpu stats without scheduling in first iteration
		virDomainPtr* domains;
		unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING |
                     VIR_CONNECT_LIST_DOMAINS_ACTIVE;
		numDomains = virConnectListAllDomains(conn, &domains, flags);
		dom_memstats = (struct domMemStats*)malloc(numDomains*sizeof(struct domMemStats));
		dom_memstats_prev = (unsigned long long*)malloc(numDomains*sizeof(unsigned long long));
		unsigned int flags2 = VIR_DOMAIN_AFFECT_LIVE;
		for(int i=0;i<numDomains;i++){
			dom_memstats[i].dptr = domains[i];
			//Set period of stats every 1 second
			if(virDomainSetMemoryStatsPeriod(dom_memstats[i].dptr,1,flags2)==-1){
				printf("Could not set period\n");
			}
			unsigned long maxMemDom = virDomainGetMaxMemory(dom_memstats[i].dptr);
			// printf("MAX MEM DOM: %lu\n",maxMemDom);
			if(maxMemDom==0){
				printf("Could not retrieve max memory");
			} else {
				dom_memstats[i].maxMemory = maxMemDom/1024;
				printf("MAX MEM DOM: %lu\n",dom_memstats[i].maxMemory);
			}
		}
		first_iter = 1;
		getMemStatsDomain();
		getMemStatsHost(conn);
		for(int i=0;i<numDomains;i++){
			dom_memstats_prev[i] = dom_memstats[i].unusedMemory;
		}
	}
	else {
		getMemStatsDomain();
		for(int i=0;i<numDomains;i++){
			printf("VM%d Unused: %llu Available: %llu\n",i,dom_memstats[i].unusedMemory,dom_memstats[i].availableMemory);
		}
		getMemStatsHost(conn);
		// sleep(2);
		int* changeInMem = (int*)malloc(numDomains*sizeof(int));
		// //Write Scheduling Algorithm
		unsigned long long memAvaToTake = 0;
		int extraMemReq = 0;
		for(int i=0;i<numDomains;i++){
			changeInMem[i] = (int)dom_memstats[i].unusedMemory-(int)dom_memstats_prev[i];
			// printf("%d \n",changeInMem[i]);
			if(changeInMem[i]>=0 && dom_memstats[i].unusedMemory>100){
				// memAvaToTake += dom_memstats[i].unusedMemory;
				memAvaToTake += 0.2*dom_memstats[i].availableMemory;
			}
			//Only give extra memory if unused memory falls by 10MB
			else if(changeInMem[i]<0){
				extraMemReq -= changeInMem[i];
			}
		}
		// if(extraMemReq==0) extraMemReq=10*numDomains;
		unsigned long long memTaken = 0;
		for(int i=0;i<numDomains;i++){
			if(changeInMem[i]>=0 && dom_memstats[i].unusedMemory>100){
				//take memory proportionate to free memory
				// double ratioOfMem = (double)dom_memstats[i].unusedMemory/(double)memAvaToTake;
		// 		printf("Ratio of Mem: %lf\n",ratioOfMem);
				// unsigned long long memTakenDom = MIN(dom_memstats[i].unusedMemory-100,extraMemReq*(ratioOfMem));
				unsigned long long memTakenDom = MIN(dom_memstats[i].unusedMemory-100,0.2*dom_memstats[i].availableMemory);
				unsigned long setMemory = (unsigned long)dom_memstats[i].availableMemory - (unsigned long)memTakenDom;
				// unsigned long setMemory = (unsigned long)(0.9*dom_memstats[i].availableMemory);
		// 		printf("%dDomain memory reduced to: %lu from: %lu\n",i,setMemory+56,setMemory+(unsigned long)memTakenDom+56);
				memTaken += memTakenDom;
				if(virDomainSetMemory(dom_memstats[i].dptr, (setMemory+56)*1024)==-1){
					printf("Unable to set memory1\n");
				} else {
					printf("Decreasing vm %d memory by %llu\n",i,memTakenDom);
				}
			}
		}
		if(memTaken<3*extraMemReq){
			if(hostMemFree>200){
				memTaken += MIN(hostMemFree-200,3*extraMemReq-memTaken);
			}
		} else {
			memTaken = 3*extraMemReq;
		}
		for(int i=0;i<numDomains;i++){
			if(changeInMem[i]<-10){
				//Give memory proportionate to change in unused memory
				unsigned long long memGivenDom = memTaken*(-1*(double)changeInMem[i]/(double)extraMemReq);
				unsigned long setMemory = (unsigned long)dom_memstats[i].availableMemory + (unsigned long)memGivenDom;
				if(virDomainSetMemory(dom_memstats[i].dptr, (MIN(setMemory+56,dom_memstats[i].maxMemory))*1024)==-1){
					printf("Unable to set memory2\n");
				} else {
					printf("Increasing vm %d memory by %llu\n",i,memGivenDom);
				}
			}
		}
		sleep(1);
		getMemStatsDomain();
		getMemStatsHost(conn);		
		for(int i=0;i<numDomains;i++){
			dom_memstats_prev[i] = dom_memstats[i].unusedMemory;
		}
	}
	printf("--------After Update-------------\n");
	for(int i=0;i<numDomains;i++){
		printf("VM%d Unused: %llu Available: %llu\n",i,dom_memstats[i].unusedMemory,dom_memstats[i].availableMemory);
	}
	printf("Host Memory: %llu\n",hostMemFree);
	sleep(1);
	// printf("Host Memory: %llu\n",hostMemFree);
	// if(virDomainSetMemory(dom_memstats[0].dptr, 1048*1024)==-1){
	// 	printf("Unable to set memory\n");
	// }
}
