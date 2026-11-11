#include "vm.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
static void mark(vm_state* vm,host_value v,uint8_t live[64]){
    uintptr_t p,base;
    if(v.tag!=HOST_VAL_NATIVE_STRING)return;
    p=(uintptr_t)v.text;base=(uintptr_t)vm->runtime_strings;
    if(v.tag==HOST_VAL_NATIVE_STRING&&p>=base&&p<base+sizeof(vm->runtime_strings))live[(p-base)/VM_STRING_LEN]=1;
}
static void mark_instance(vm_state* vm,const script_instance* inst,uint8_t live[64]){
    unsigned i,j;
    if(!inst)return;
    for(i=0;i<VM_FIELD_CAP;++i)mark(vm,inst->runtime_fields[i],live);
    for(i=0;i<inst->array_count;++i)for(j=0;j<inst->arrays[i].length;++j)mark(vm,inst->arrays[i].values[j],live);
    for(i=0;i<VM_COROUTINE_CAP;++i)if(inst->coroutines[i].active){
        for(j=0;j<inst->coroutines[i].sp;++j)mark(vm,inst->coroutines[i].stack[j],live);
        for(j=0;j<VM_LOCAL_CAP;++j)mark(vm,inst->coroutines[i].locals[j],live);
    }
}
host_value vm_make_string(vm_state* vm,const char* text){
    host_value v={0};unsigned i,j;uint8_t live[64]={0};
    if(!text)text="";
    if(strlen(text)>=VM_STRING_LEN){host_log("String exceeds PS1 capacity");vm->error=1;return v;}
    for(i=0;i<64;++i)if(vm->runtime_string_used[i]&&!strcmp(vm->runtime_strings[i],text)){v.tag=HOST_VAL_NATIVE_STRING;v.text=vm->runtime_strings[i];return v;}
    for(i=0;i<64&&vm->runtime_string_used[i];++i){}
    if(i==64){
        for(j=0;j<vm->sp;++j)mark(vm,vm->stack[j],live);
        for(j=0;j<VM_LOCAL_CAP;++j)mark(vm,vm->locals[j],live);
        for(j=0;j<vm->instance_count;++j)mark_instance(vm,&vm->instances[j],live);
        mark_instance(vm,vm->instance,live);
        for(j=0;j<64;++j)vm->runtime_string_used[j]=live[j];
        for(i=0;i<64&&live[i];++i){}
    }
    if(i==64){host_log("Live string capacity exhausted");vm->error=1;return v;}
    strcpy(vm->runtime_strings[i],text);vm->runtime_string_used[i]=1;v.tag=HOST_VAL_NATIVE_STRING;v.text=vm->runtime_strings[i];return v;
}
static int string_value(host_value v){return v.tag==HOST_VAL_STRING||v.tag==HOST_VAL_NATIVE_STRING;}
static void concat_text(vm_state* vm,host_value v,char out[VM_STRING_LEN]){
    if(string_value(v)){snprintf(out,VM_STRING_LEN,"%s",vm_value_string(vm,v));return;}
    if(v.tag==HOST_VAL_NUMBER){
        uint32_t magnitude=v.ival<0?0u-(uint32_t)v.ival:(uint32_t)v.ival;
        unsigned fraction=(unsigned)(((uint64_t)(magnitude&65535u)*1000000u+32768u)>>16);
        snprintf(out,VM_STRING_LEN,"%s%u.%06u",v.ival<0?"-":"",(unsigned)(magnitude>>16),fraction);return;
    }
    out[0]=0;
}
host_value vm_add_values(vm_state* vm,host_value a,host_value b){
    host_value v={0};
    if((string_value(a)&&string_value(b))||(string_value(a)&&b.tag==HOST_VAL_NUMBER)||(a.tag==HOST_VAL_NUMBER&&string_value(b))){
        char left[VM_STRING_LEN],right[VM_STRING_LEN],text[VM_STRING_LEN];unsigned n;
        concat_text(vm,a,left);concat_text(vm,b,right);n=(unsigned)strlen(left);
        if(n+strlen(right)>=sizeof(text)){host_log("Concatenated string exceeds PS1 capacity");vm->error=1;return v;}
        memcpy(text,left,n);strcpy(text+n,right);return vm_make_string(vm,text);
    }
    v.tag=HOST_VAL_NUMBER;v.ival=fix16_add(a.tag==HOST_VAL_NUMBER?a.ival:0,b.tag==HOST_VAL_NUMBER?b.ival:0);return v;
}
static void mark_array(host_value v,uint8_t live[VM_ARRAY_CAP]){
    if(v.tag==HOST_VAL_ARRAY&&v.ival>0&&v.ival<=VM_ARRAY_CAP)live[v.ival-1]=1;
}
host_value vm_allocate_array(vm_state* vm,script_instance* inst,int length){
    host_value value={0};uint8_t live[VM_ARRAY_CAP]={0};unsigned i,j,slot;
    if(length<0||length>VM_ARRAY_LENGTH){host_log("Array length exceeds PS1 capacity");vm->error=1;return value;}
    if(inst->array_count<VM_ARRAY_CAP)slot=inst->array_count++;
    else{
        for(i=0;i<VM_FIELD_CAP;++i)mark_array(inst->runtime_fields[i],live);
        for(i=0;i<vm->sp;++i)mark_array(vm->stack[i],live);
        for(i=0;i<VM_LOCAL_CAP;++i)mark_array(vm->locals[i],live);
        for(i=0;i<VM_COROUTINE_CAP;++i)if(inst->coroutines[i].active){
            for(j=0;j<inst->coroutines[i].sp;++j)mark_array(inst->coroutines[i].stack[j],live);
            for(j=0;j<VM_LOCAL_CAP;++j)mark_array(inst->coroutines[i].locals[j],live);
        }
        /* Close over nested arrays; cycles need no recursion. */
        for(unsigned pass=0;pass<VM_ARRAY_CAP;++pass)for(i=0;i<VM_ARRAY_CAP;++i)if(live[i])
            for(j=0;j<inst->arrays[i].length;++j)mark_array(inst->arrays[i].values[j],live);
        for(slot=0;slot<VM_ARRAY_CAP&&live[slot];++slot){}
        if(slot==VM_ARRAY_CAP){host_log("Live array capacity exhausted");vm->error=1;return value;}
    }
    memset(&inst->arrays[slot],0,sizeof(inst->arrays[slot]));inst->arrays[slot].length=(uint8_t)length;
    value.tag=HOST_VAL_ARRAY;value.ival=(int32_t)slot+1;return value;
}
