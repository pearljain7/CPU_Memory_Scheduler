#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<libvirt/virterror.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

typedef struct memStats{
	virDomainPtr domain;
	unsigned long unused; //how much unused memory is present
	unsigned long available;//how much memory is available for usage
}memStats;

typedef struct memStats * memStatsPtr;

static unsigned long getFreeMemInHost(virConnectPtr conn){
	int nparams=4;
	unsigned long freeMem;
	
	virNodeMemoryStatsPtr params = malloc(sizeof(virNodeMemoryStats) * nparams);
	memset(params, 0, sizeof(virNodeMemoryStats) * nparams);
	virNodeGetMemoryStats(conn,VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, 0);
		
	for(int i=0; i < nparams; i++){
		 if(strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE)==0){
			freeMem = params[i].value;break;
		}
	}
	free(params);
	return freeMem;
}

void MemoryScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
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
	virDomainPtr * activeDomains = NULL;
	int numDomains = virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING);
	printf("num of domains %d",numDomains);
	if(numDomains==-1){
		printf("unable to get list of domains");
		return -1;
	}

	memStatsPtr memDomains = malloc(sizeof(memStats)*numDomains);	
	for(int i=0; i<numDomains; i++){
		virDomainSetMemoryStatsPeriod(activeDomains[i], interval, VIR_DOMAIN_AFFECT_CURRENT);			
	}

	int LOADED_THRESHOLD = 1024;
	int FREE_THRESHOLD = 1024;

	for(int i=0; i<numDomains; i++){
			memDomains[i].domain = activeDomains[i];
			int nr_stats = VIR_DOMAIN_MEMORY_STAT_NR;
			printf("num of stats %d",nr_stats);
			virDomainMemoryStatPtr stats = malloc(sizeof(virDomainMemoryStatStruct)*nr_stats);
			virDomainMemoryStats(activeDomains[i], stats, nr_stats, 0);
			for(int i=0; i<nr_stats; i++){
				if(stats[i].tag==VIR_DOMAIN_MEMORY_STAT_UNUSED){
					memDomains[i].unused = stats[i].val;
				}
				if(stats[i].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
					memDomains[i].available = stats[i].val;
				}

			}
	}

	//most and least used memory
	int most=0, least=0;
		unsigned long mostMem =memDomains[0].unused, leastMem=memDomains[0].unused;
		for(int i=1; i<numDomains; i++){
			if(memDomains[i].unused > mostMem){
				most = i;
				mostMem = memDomains[i].unused;
			}
			if(memDomains[i].unused < leastMem){
				leastMem = memDomains[i].unused;
				least = i;
			}
		}

	//	if unused memory
	if(memDomains[least].unused <= LOADED_THRESHOLD){
			//if the domain with most free memory can afford to give memory, take memory away
			if(memDomains[most].unused >= FREE_THRESHOLD){
				//balloon can be inflated 
				virDomainSetMemory(memDomains[most].domain, memDomains[most].available-LOADED_THRESHOLD);
				virDomainSetMemory(memDomains[least].domain, memDomains[least].available+LOADED_THRESHOLD);
			} else { //give the memory from host
				virDomainSetMemory(memDomains[least].domain, memDomains[least].available+FREE_THRESHOLD);
			}
		} else if(memDomains[most].unused >= FREE_THRESHOLD){ //there is a domain which is using unnecessary memory
			virDomainSetMemory(memDomains[most].domain, memDomains[most].available-LOADED_THRESHOLD);
		}

	unsigned long memFreeInHost = getFreeMemInHost(conn);
	
	if(memFreeInHost <=LOADED_THRESHOLD){
			//go through all domains and take away memory wherever possible
			for(int i=0; i< numDomains; i++){
				if(memDomains[i].unused >= FREE_THRESHOLD){
					virDomainSetMemory(memDomains[i].domain, memDomains[i].available-LOADED_THRESHOLD);
				}
			}
		}

	//sleep(interval);

	for(int i=0; i<numDomains; i++){
		virDomainFree(activeDomains[i]);
	}
	free(activeDomains);
}



