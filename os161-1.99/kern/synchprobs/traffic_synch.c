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

enum passType {
    none = 0,
    nw = 1,
    ew = 2,
    ne = 3,
    es = 4,
    sw = 5,
    wn = 6,
};


static volatile int volatile enter[4] = {0, 0, 0, 0};
static volatile int volatile exit[4] = {0, 0, 0, 0};

static volatile int total = 0;
static volatile bool first_entry = false;
static volatile bool warning = false;

static struct lock *mutex;
static struct cv *cv_traffic;

void changeEnter(Direction o, int i);
void changeExit(Direction o, int i);
bool parallel(Direction o, Direction d);
bool opposite(Direction o, Direction d);
bool rightTurn(Direction o, Direction d);
bool legalRightTurn(Direction o, Direction d);
bool checkConstraint(Direction o, Direction d);

void 
changeEnter(Direction o, int i){
    enter[o] += i;
}

void 
changeExit(Direction o, int i){
    exit[o] += i;
}

bool
parallel(Direction o, Direction d){
    return enter[o] > 0 && exit[d] > 0;
}

bool
opposite(Direction o, Direction d){
    return enter[d] > 0 && exit[o] > 0;
}

bool
legalRightTurn(Direction o, Direction d){
    if (exit[d] > 0){
        return false;
    }
    
    if (o == north && d == west){
        return true;
    }
    
    return o - 1 == d;
}

bool 
rightTurn(Direction o, Direction d){
    return o - 1 == d || (o == north && d == west);
}

bool 
checkConstraint(Direction o, Direction d){
    if (warning){
        return false;
    }else if (total == 0){
        if (!rightTurn(o,d)){
            changeEnter(o,1);
            changeExit(o,1);
        }
        return true;
    }else if (parallel(o,d) || opposite(o,d)){
        changeEnter(o,1);
        changeExit(d,1);
        return true;
    } else if (legalRightTurn(o,d)){
        return true;
    }
    
    return false;
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
    while (!checkConstraint(o,d)){
        cv_wait(cv_traffic, mutex);
    }
    
    total++;
    
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
  
    if (!rightTurn(o,d)){
        changeEnter(o, -1);
        changeExit(d, -1);
    }
    total--;
    
    cv_broadcast(cv_traffic, mutex);
    
    lock_release(mutex);
}
