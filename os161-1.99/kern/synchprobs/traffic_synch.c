#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <current.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

static volatile int volatile enterBlock[4] = {0, 0, 0, 0};
static volatile int volatile regularBlock[4] = {0, 0, 0, 0};
static volatile int volatile rightTurnBlock[4] = {0, 0, 0, 0};

static volatile int total = 0;
static volatile bool first_entry = false;
static volatile bool warning = false;

static struct lock *mutex;
static struct cv *cv_traffic;

void setEnter(Direction o, int i);
void setExit(Direction o, int i);
void setRightTurnBlock(Direction o, int i);
void setBlock(Direction o, Direction d, int i);
bool checkConstraint(Direction o, Direction d);

void 
setEnter(Direction o, int i){
    enterBlock[o] += i;
}

void 
setExit(Direction o, int i){
    regularBlock[o] += i;
}

void
setRightTurnBlock(Direction o, int i){
    rightTurnBlock[o] += i;
}

/*
 * i = 1 indicates entering
 * i = -1 indicates leaving
 */
void
setBlock(Direction o, Direction d, int i){
    // straight pass
    if ((o == east && d == west) || (o == west && d == east)){
        setEnter(0, i); // block/unblock from north entering (Non right turn)
        setEnter(2, i); // block/unblock from south entering (Non right turn)
        setExit(1, i);
        setExit(3, i);
        setRightTurnBlock(d, i);
    }else if ((o == north && d == south) || (o == south && d == north)){
        setEnter(1, i); // block/unblock from east entering
        setEnter(3, i); // block/unblock from west entering
        setExit(0, i);
        setExit(2, i);
        setRightTurnBlock(d, i);
    }else if ((o == west && d == north) || ( o + 1 == d)){
        for (unsigned int j = 0; j < 4; j++){
            if (j != o)
                setEnter(j, i); // block all except the current one
            if (j != d)
                setExit(j, i);
        }
        setRightTurnBlock(d, i);
    }else if ((o == north && d == west) || ( o - 1 == d)){
        setExit(d,i);
    }
}

bool 
checkConstraint(Direction o, Direction d){
    if ((o == north && d == west) || ( o - 1 == d)){
        return rightTurnBlock[d];
    }
    
    return enterBlock[o] || regularBlock[d];
}


/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
    mutex = lock_create("traffic lock");
    cv_traffic = cv_create("traffic cv");
    
    if (mutex == NULL || cv_traffic == NULL){
        panic("uh-oh.....");
    }
    
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  KASSERT(mutex != NULL);
  KASSERT(cv_traffic != NULL);
  
  lock_destroy(mutex);
  cv_destroy(cv_traffic);

}

/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction o, Direction d) 
{
    KASSERT(mutex != NULL);
    KASSERT(cv_traffic != NULL);
    
    lock_acquire(mutex);
    
    while (checkConstraint(o,d)){
        cv_wait(cv_traffic, mutex);
    }
    
    setBlock(o,d,1);
    
    lock_release(mutex);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction o, Direction d) 
{
    KASSERT(mutex != NULL);
    KASSERT(cv_traffic != NULL);
    
    lock_acquire(mutex);
    
    setBlock(o,d,-1);
    
    cv_broadcast(cv_traffic, mutex);
    
    lock_release(mutex);
}
