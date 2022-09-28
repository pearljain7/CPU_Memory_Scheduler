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

static const int STARVATION_THRESHOLD = 100 * 1024;

// Define an available memory threshold above which a domain can be
// considered to be wasting memory (in MB)
static const int WASTE_THRESHOLD = 512 * 1024;

struct DomainsList domains_list(virConnectPtr conn);
struct DomainsList active_domains(virConnectPtr conn);

struct DomainMemory {
	virDomainPtr domain;
	long memory;
};

struct DomainsList {
	virDomainPtr *domains; /* pointer to array of Libvirt domains */
	int count;             /* number of domains in the *domains array */
};

struct DomainsList active_domains(virConnectPtr conn)
{
	
	struct DomainsList fetchlist = domains_list(conn);
	return fetchlist;
}

struct DomainsList domains_list(virConnectPtr conn)
{
	virDomainPtr *domains;
	int num_domains;
	num_domains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_ACTIVE |
		VIR_CONNECT_LIST_DOMAINS_RUNNING);
	//check(num_domains > 0, "Failed to list all domains\n");
	struct DomainsList *list = malloc(sizeof(struct DomainsList));
	list->count = num_domains;
	list->domains = domains;
	return *list;
}

char *tagToMeaning(int tag)
{
	char *meaning;

	switch (tag) {
	case VIR_DOMAIN_MEMORY_STAT_SWAP_IN:
		meaning = "SWAP IN";
		break;
	case VIR_DOMAIN_MEMORY_STAT_SWAP_OUT:
		meaning = "SWAP OUT";
		break;
	case VIR_DOMAIN_MEMORY_STAT_MAJOR_FAULT:
		meaning = "MAJOR FAULT";
		break;
	case VIR_DOMAIN_MEMORY_STAT_MINOR_FAULT:
		meaning = "MINOR FAULT";
		break;
	case VIR_DOMAIN_MEMORY_STAT_UNUSED:
		meaning = "UNUSED";
		break;
	case VIR_DOMAIN_MEMORY_STAT_AVAILABLE:
		meaning = "AVAILABLE";
		break;
	case VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON:
		meaning = "CURRENT BALLOON";
		break;
	case VIR_DOMAIN_MEMORY_STAT_RSS:
		meaning = "RSS (Resident Set Size)";
		break;
	case VIR_DOMAIN_MEMORY_STAT_NR:
		meaning = "NR";
		break;
	}

	return meaning;
}

static unsigned long getFreeMemInHost(virConnectPtr conn){
	int nparams=4;
	unsigned long freeMem;
	
	virNodeMemoryStatsPtr params = malloc(sizeof(virNodeMemoryStats) * nparams);
	memset(params, 0, sizeof(virNodeMemoryStats) * nparams);
	virNodeGetMemoryStats(conn,VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, 0);
	//printf("free memory %d",VIR_NODE_MEMORY_STATS_ALL_CELLS);
		
	for(int i=0; i < nparams; i++){
		 if(strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE)==0){
			freeMem = params[i].value;
			break;
		}
	}
	free(params);
	return freeMem;
}

void printDomainStats(struct DomainsList list)
{
	printf("------------------------------------------------\n");
	printf("%d memory stat types supported by this hypervisor\n",
	       VIR_DOMAIN_MEMORY_STAT_NR);
	printf("------------------------------------------------\n");
	for (int i = 0; i < list.count; i++) {
		virDomainMemoryStatStruct memstats[VIR_DOMAIN_MEMORY_STAT_NR];
		unsigned int nr_stats;
		unsigned int flags = VIR_DOMAIN_AFFECT_CURRENT;

		virDomainSetMemoryStatsPeriod(list.domains[i], 1, flags);
		nr_stats = virDomainMemoryStats(list.domains[i],
						memstats,
						VIR_DOMAIN_MEMORY_STAT_NR,
						0);
		for (int j = 0; j < nr_stats; j++) {
			printf("%s : %s = %llu MB\n",
			       virDomainGetName(list.domains[i]),
			       tagToMeaning(memstats[j].tag),
			       memstats[j].val/1024);
		}
	}
}

void printHostMemoryStats(virConnectPtr conn)
{
	int nparams = 0;
	virNodeMemoryStatsPtr stats = malloc(sizeof(virNodeMemoryStats));

	if (virNodeGetMemoryStats(conn,
				  VIR_NODE_MEMORY_STATS_ALL_CELLS,
				  NULL,
				  &nparams,
				  0) == 0 && nparams != 0) {
		stats = malloc(sizeof(virNodeMemoryStats) * nparams);
		memset(stats, 0, sizeof(virNodeMemoryStats) * nparams);
		virNodeGetMemoryStats(conn,
				      VIR_NODE_MEMORY_STATS_ALL_CELLS,
				      stats,
				      &nparams,
				      0);
	}
	printf("Hypervisor memory:\n");
	for (int i = 0; i < nparams; i++) {
		printf("%8s : %lld MB\n",
		       stats[i].field,
		       stats[i].value/1024);
	}
}



struct DomainMemory *findRelevantDomains(struct DomainsList list)
{
	struct DomainMemory *ret;
	struct DomainMemory wasteful;
	struct DomainMemory starved;

	ret = malloc(sizeof(struct DomainMemory) * 2);
	wasteful.memory = 0;
	starved.memory = 0;
	for (int i = 0; i < list.count; i++) {
		virDomainMemoryStatStruct memstats[VIR_DOMAIN_MEMORY_STAT_NR];
		unsigned int nr_stats;
		unsigned int flags = VIR_DOMAIN_AFFECT_CURRENT;
		unsigned int period_enabled;

		period_enabled = virDomainSetMemoryStatsPeriod(list.domains[i],
							       1,
							       flags);
		//check(period_enabled >= 0,
		      //"ERROR: Could not change balloon collecting period");
		nr_stats = virDomainMemoryStats(list.domains[i], memstats,
						VIR_DOMAIN_MEMORY_STAT_NR, 0);
		//check(nr_stats != -1,
		      //"ERROR: Could not collect memory stats for domain %s",
		      //virDomainGetName(list.domains[i]));
		printf("%s : %llu MB available\n",
		       virDomainGetName(list.domains[i]),
		       (memstats[VIR_DOMAIN_MEMORY_STAT_AVAILABLE].val)/1024);
		if (memstats[VIR_DOMAIN_MEMORY_STAT_AVAILABLE].val > wasteful.memory) {
			wasteful.domain = list.domains[i];
			wasteful.memory = memstats[VIR_DOMAIN_MEMORY_STAT_AVAILABLE].val;
		}
		if (memstats[VIR_DOMAIN_MEMORY_STAT_AVAILABLE].val < starved.memory ||
		    starved.memory == 0) {
			starved.domain = list.domains[i];
			starved.memory = memstats[VIR_DOMAIN_MEMORY_STAT_AVAILABLE].val;
		}
	}
	printf("%s is the most wasteful domain - %ld MB available\n",
	       virDomainGetName(wasteful.domain),
	       wasteful.memory/1024);
	printf("%s is the domain that needs the most memory - %ld MB available\n",
	       virDomainGetName(starved.domain),
	       starved.memory/1024);

	ret[0] = wasteful;
	ret[1] = starved;
	return ret;
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
	struct DomainsList list;

	while ((list = active_domains(conn)).count > 0) {
		struct DomainMemory *relevantDomains;
		struct DomainMemory wasteful;
		struct DomainMemory starved;

		relevantDomains = findRelevantDomains(list);
		wasteful = relevantDomains[0];
		starved = relevantDomains[1];
		free(relevantDomains);

		if (starved.memory <= STARVATION_THRESHOLD) { //- domain 100
		// At this point, we must assign more memory to the domain
			if (wasteful.memory >= WASTE_THRESHOLD) { //2048
				// The most wasteful domain will get less memory, precisely
				// 'waste/2', and the most starved domain will get
				// removed the same quantity.
				printf("Removing memory from wasteful domain\n");
				virDomainSetMemory(wasteful.domain,
						   wasteful.memory - wasteful.memory/2);
				printf("Adding memory to starved domain\n");
				virDomainSetMemory(starved.domain,
						   starved.memory + wasteful.memory/2);
			} else {
				// There is not any waste (< WASTE_THRESHOLD) and a domain is
				// critical (< STARVATION_THRESHOLD). //200 - host 
				// Assign memory from the hypervisor until the starved host
				// has STARVATION_THRESHOLD available.
				//
				// You need to be generous assigning memory,
				// otherwise it's consumed immediately (in
				// between coordinator periods)
				printf("Adding memory to starved domain %s\n",
				       virDomainGetName(starved.domain));
				printf("starved domain memory is %lu\n",
				       starved.memory/1024);
				virDomainSetMemory(starved.domain,
						   starved.memory + WASTE_THRESHOLD);
			}
		} else if (wasteful.memory >= WASTE_THRESHOLD) {
			// No domain really need more memory at this point, give
			// it back to the hypervisor
			printf("Returning memory back to host\n");
			virDomainSetMemory(wasteful.domain,
					   wasteful.memory - WASTE_THRESHOLD);
			printf("DONE\n");
		}

		unsigned long memFreeInHost = getFreeMemInHost(conn);
		printf("\nfree memory %ld",memFreeInHost);
	}

}