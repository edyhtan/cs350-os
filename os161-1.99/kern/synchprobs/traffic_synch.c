#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

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

enum Passes;

enum Passes{
    initial = -1,
    warning = 0,
    ew = 1,
    ns = 2,
    es = 3,
    sw = 4,
    wn = 5,
    ne = 6
};

typedef enum Passes Pass;


static volatile Pass traffic_light = initial; 
static volatile bool first_reach = false;
static volatile int carPasses = 0;

static struct lock *mutex;
static struct cv *cv_traffic;

void setWarning(void);
void setInitial(void);
bool setRules(Direction o, Direction d);
bool canPass(Direction o, Direction d);
bool isRightTurn(Direction o, Direction d);
bool isLegalRightTurn(Direction o, Direction d);

void 
setWarning(){
    traffic_light = warning;
    kprintf( "Warning\n" );
}

void 
setInitial(){
    traffic_light = initial;
    kprintf( "Rules End\n" );
}

bool
setRules(Direction o, Direction d){
    if (traffic_light != initial || isRightTurn(o,d)){
        return false;
    }else if ((o == east && d == west) || (o == west && d == east)){
        traffic_light = ew;
    }else if ((o == north && d == south) || (o == south && d == north)){
        traffic_light = ns;
    }else if (o == east && d == south){
        traffic_light = es;
    }else if (o == south && d == west){
        traffic_light = sw;
    }else if (o == west && d == north){
        traffic_light = wn;
    }else if (o == north && d == east){
        traffic_light = ne;
    }else {
        return false;
    }
    
    kprintf( "Rules Set %d -> %d\n", o, d );
    return true;
}

bool
canPass(Direction o, Direction d){
    if (traffic_light == warning || isRightTurn(o,d)){
        return false;
    }else if (traffic_light == ew){
        return (o == east && d == west) || (o == west && d == east);
    }else if (traffic_light == ns){
        return (o == north && d == south) || (o == south && d == north);
    }else if (traffic_light == es){
        return (o == east && d == south);
    }else if  (traffic_light == sw){
        return (o == south && d == west);
    }else if (traffic_light == wn){
        return (o == west && d == north);
    }else if (traffic_light == ne){
        return (o == north && d == east);
    }

    panic("uh-oh");
    return false;
}

bool
isRightTurn(Direction o, Direction d){
    return (o == east && d == north) || (o == north && d == west) || (o == west && d == south) || (o == south && d == east);
}

bool
isLegalRightTurn(Direction o, Direction d){
    
    if (traffic_light == initial){
        setWarning();
        return true;
    }else if (d == north && (traffic_light == ns || traffic_light == wn)){
        return false;
    }else if (d == south && (traffic_light == ns || traffic_light == es)){
        return false;
    }else if (d == east && (traffic_light == ew || traffic_light == ne)){
        return false;
    }else if (d == west && (traffic_light == ew || traffic_light == sw)){
        return false;
    }
    
    return isRightTurn(o,d);
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
    while (!isLegalRightTurn(o,d) && !setRules(o,d) && !canPass(o,d)){
        cv_wait(cv_traffic, mutex);
    }
    
    kprintf("car enter: %d %d", o, d);
    
    if (first_reach == false)
        first_reach = true;
    
    if (!isRightTurn(o,d))
        carPasses++;
        
    cv_signal(cv_traffic, mutex);
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
intersection_after_exit(Direction origin, Direction destination) 
{
    KASSERT(mutex != NULL);
    KASSERT(cv_traffic != NULL);
    
    lock_acquire(mutex);
  
    if (first_reach && !warning){
      first_reach = false;
      setWarning();
    }
    
    if (!isRightTurn(origin, destination)) 
        carPasses--;
  
    if (carPasses == 0)
        setInitial();

    cv_broadcast(cv_traffic, mutex);
    lock_release(mutex);
}
