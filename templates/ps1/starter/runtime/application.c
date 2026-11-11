#include "application.h"
static int s_quit_requested;
void ps1_application_request_quit(void){s_quit_requested=1;}
int ps1_application_quit_requested(void){return s_quit_requested;}
void ps1_application_process_quit(void){
    if(s_quit_requested){s_quit_requested=0;ps1_platform_quit();}
}
