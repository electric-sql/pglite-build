#ifndef PG_EXTERN
#define PG_EXTERN


extern MemoryContext getCurrentMemoryContext();
extern void setCurrentMemoryContext(MemoryContext ctx);

extern MemoryContext setMemoryContextSwitch(MemoryContext context);


extern ErrorContextCallback *get_error_context_stack();
extern void set_error_context_stack(ErrorContextCallback *errcs);

extern MemoryContext getTopMemoryContext();

extern bool get_check_function_bodies();
#endif
