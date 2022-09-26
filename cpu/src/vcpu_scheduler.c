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

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

typedef struct getDomainStats {
	virDomainPtr domain;
	long time;
	double usage;
	int cpuNum;

} getDomainStats;

typedef getDomainStats * domainPtr;

typedef struct pcpuStats{
	double usage;
} pcpuStats;

typedef pcpuStats * pcpuPtr; 

void CPUScheduler(virConnectPtr conn,int interval);

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

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{

	printf("\nRunning code");
	pcpuPtr pcpuStats = NULL;
	int prevNumDomains; 
	virDomainPtr * activeDomains = NULL;
	
	int numDom =  virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING);

	domainPtr currDomainStats = malloc(numDom*sizeof(getDomainStats));
	memset(currDomainStats,0,numDom*sizeof(getDomainStats));

	domainPtr prevDomainStats=NULL;
	prevDomainStats = malloc(numDom*sizeof(getDomainStats));	
	//get current vcpu time
	for(int i=0;i<numDom; i++){
		currDomainStats[i].domain = activeDomains[i];
		int nparams = virDomainGetCPUStats(activeDomains[i], NULL, 0, -1, 1, 0);

		virTypedParameterPtr params = calloc(nparams, sizeof(virTypedParameter));
		virDomainGetCPUStats(activeDomains[i], params, nparams, -1, 1, 0); 

		unsigned long long cpuTime;
		for(int j=0; j<nparams; j++){
		if(strcmp(params[j].field, "cpu_time")==0){
			cpuTime = params[j].value.ul;
			printf("\nCpu time %ld",cpuTime);
			break;
		}
	}
			currDomainStats[i].time =cpuTime;
			currDomainStats[i].usage =0.0;
			currDomainStats[i].cpuNum =-1;
		
	}
		virNodeInfoPtr nodeInfo = malloc(sizeof(virNodeInfo));
		int numPcpus = 0;
		pcpuPtr currPcpuStats;
		if(virNodeGetInfo(conn, nodeInfo)==-1){
			printf("Unable to get node info");
		} else {
			numPcpus = nodeInfo->cpus;
			currPcpuStats = malloc(numPcpus*sizeof(pcpuStats));
			memset(currPcpuStats, 0, numPcpus*sizeof(pcpuStats));
		}

		//if loop 
		if(prevDomainStats!=NULL){
			//calc domain usage
			for(int i=0; i<numDom; i++){
				currDomainStats[i].usage = 100*(currDomainStats[i].time - prevDomainStats[i].time)/(interval*pow(10,9));
				printf("\nprinting current domain stats usage: %f",currDomainStats[i].usage);
			}
			
			//calc pcpu usage 
			for(int i=0; i<numDom; i++){
				virVcpuInfoPtr info = malloc(sizeof(virVcpuInfo));
				virDomainGetVcpus(currDomainStats[i].domain, info, 1, NULL, 0);
				currPcpuStats[info->cpu].usage += currDomainStats[i].usage; 
				currDomainStats[i].cpuNum = info->cpu;
				printf(" current Time is %f",currDomainStats[i].usage);
			}

			//pinvpcu
			double maxUsage = currPcpuStats[0].usage, minUsage = currPcpuStats[0].usage;
			int freestPcpu = 0, busiestPcpu =0;
			for(int i=1; i<numPcpus; i++){
			
				if(currPcpuStats[i].usage < minUsage){
					freestPcpu = i;
					minUsage = currPcpuStats[i].usage;
				}
				if(currPcpuStats[i].usage > maxUsage){
					busiestPcpu = i;
					maxUsage = currPcpuStats[i].usage;
				}
			}

			
			int busiestDomain = -1;
			unsigned char cpuMap;
			if(maxUsage - minUsage > 10.0) {
				//in the pcpu with heaviest load find the busiest domain
				
				maxUsage = INT_MIN;
				for(int i=0; i<numDom; i++){

					if(currDomainStats[i].cpuNum == busiestPcpu){
						if(currDomainStats[i].usage > maxUsage){
							busiestDomain = i;
							maxUsage = currDomainStats[i].usage;
						}
					}
				}
				cpuMap = 0x1 << freestPcpu;
				
				//pin the busiest domain to freest pcpu
				if(virDomainPinVcpu(currDomainStats[busiestDomain].domain, 0, &cpuMap, (numPcpus/8)+1)==-1){
					printf("Unable to pin vcpu to pcpu");
				}
			}
			//for all other domains, just pin it to the cpu they were running on
			for(int i=0; i<numDom; i++){
				if(i!=busiestDomain){
					int cpu = currDomainStats[i].cpuNum;
					cpuMap = 0x1 << cpu;
					if(virDomainPinVcpu(currDomainStats[i].domain, 0, &cpuMap, (numPcpus/8)+1)==-1){
						printf("Unable to pin vcpu to pcpu");
					}
				}
			}
//end
		}
		free(currPcpuStats);
		prevNumDomains = numDom;
				
		memcpy(prevDomainStats, currDomainStats,numDom * sizeof(getDomainStats));
		free(currDomainStats);
	

	free(pcpuStats);
	free(prevDomainStats);
}




