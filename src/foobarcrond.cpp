//============================================================================
// Name        : spire.cpp
// Author      : me
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/uuid.h>
#include <sys/stat.h>
#include <vector>

struct jd_t { // job descriptor
	char * commandline;
	time_t when_to_start;;
	char * wd;
	pthread_t thr;
	int status;
	~jd_t() { delete commandline;  delete wd;}
};

std::vector<jd_t *> All_jobs;

#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)

extern void setup_handler(int);

pthread_mutex_t jcreate_mutex;

void * wait_and_start_job(void * desc)
{
	jd_t * job = ((jd_t *)desc);

	char buf[BUFSIZ];
    time_t now;
    struct tm * timeinfo;
    time (&now);

    while(now < job->when_to_start) {
    	sleep(1);
    	time (&now);
    }

    pthread_mutex_lock(&jcreate_mutex); // protect the wd creation

    // wd is made using the pattern "/home/foobaruser/commandline.randomnumber"
    sprintf(buf,"%s/%s.%d",job->wd,job->commandline,rand());
    job->wd = strdup(buf);
    int wdstat = mkdir(job->wd,0777);
    if(wdstat != -1) {
    	mkdir(job->wd,0777);
    } else {
    	printf("problem creating unique working directory name : %s\n",job->wd);
    	exit(EXIT_FAILURE);
    }

    pthread_mutex_unlock(&jcreate_mutex);

    printf ( "job : %s starts at %lld\n", job->commandline, (long long)now);

    sprintf(buf,"rsh localhost \"cd %s && %s \"",job->wd,job->commandline);
//    printf("command : %s\n",buf);
    int ret = system(buf);
    if(ret == -1) {
        handle_error_en(ret, "system");
    }
    printf("job : %s finished, status : %d\n",job->commandline,WEXITSTATUS(ret));
    sprintf(buf,"echo %d > %s/status",WEXITSTATUS(ret),job->wd);

//    printf("writing status with command %s\n",buf);
    ret = system(buf);
    if(ret == -1) {
        handle_error_en(ret, "system");
    }
    printf("user notified to get results and status from the directory : %s\n",job->wd);
    delete job;
}

int runjob(char * cml,time_t when,char * basewd)
{
	time_t curtime;
	time ( &curtime );
	if(when < curtime) {
        printf("cant't run jobs in the past\n");
        return -1;
	}

    int s;
    pthread_attr_t attr;
    int stack_size = 0x10000;

    /* Initialize thread creation attributes */

    s = pthread_attr_init(&attr);
    if (s != 0)
        handle_error_en(s, "pthread_attr_init");

    // for better scalability, reduce the default stack size
    s = pthread_attr_setstacksize(&attr, stack_size);
    if (s != 0)
        handle_error_en(s, "pthread_attr_setstacksize");

    jd_t * j = new jd_t;
    j->commandline = cml;
    j->when_to_start = when;
    j->wd = basewd;

    s = pthread_create(&j->thr, &attr,
                             &wait_and_start_job, j);
    All_jobs.push_back(j);

    if (s != 0)
        handle_error_en(s, "pthread_create");

    s = pthread_attr_destroy(&attr);
    if (s != 0)
        handle_error_en(s, "pthread_attr_destroy");

}


int main(int argc,char * argv[]) {

	char buf[BUFSIZ];
	char * basewd;

	if(argc == 1) {
		basewd = (char *)"/home/foobaruser";
	} else {
		basewd = argv[1];
	}

    printf("%s is running using the base wd : %s\n",argv[0],basewd);

    while(fgets(buf,BUFSIZ,stdin)) { // read the user's jobs description from stdin

        buf[strlen(buf)-1] = '\0'; // remove the newline
	    time_t now;
	    struct tm * timeinfo;

	//   seed for a new sequence of pseudo-random integers
	    srand((unsigned) time (&now));

	    char * p = &buf[0];
	    char * cml = strsep(&p," ");
	    cml = strdup(cml);

	    int seconds_from_now;
	    if(sscanf(p,"%d",&seconds_from_now) != 1) {
	    	handle_error("Bad input");
	    }

	    time_t when_to_run = now + seconds_from_now;
	    printf ( "current time : %lld job : %s: scheduled to run at time : %lld\n",
	    		(long long)now, cml, (long long)when_to_run);

	    runjob(cml,when_to_run,basewd);

	    sleep(1);
    }

    // when the input is exhausted, the daemon waits for all threads to finish

    std::vector<jd_t *>::iterator it = All_jobs.begin();
    for(; it != All_jobs.end(); it++) {
        pthread_join((*it)->thr,0);
    }

	return 0;
}
