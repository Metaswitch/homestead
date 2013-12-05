#include <signal.h>
#include <semaphore.h>

#include "diameterstack.h"

static sem_t term_sem;

// Signal handler that triggers sprout termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

int main(int argc, char**argv)
{
  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  DiameterStack* diameter_stack = DiameterStack::getInstance();
  try
  {
    diameter_stack->initialize();
    //diameter_stack.configure();
    diameter_stack->start();
  }
  catch (DiameterStack::Exception& e)
  {
  }

  sem_wait(&term_sem);

  try
  {
    diameter_stack->stop();
    diameter_stack->wait_stopped();
  }
  catch (DiameterStack::Exception& e)
  {
  }

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);
}
