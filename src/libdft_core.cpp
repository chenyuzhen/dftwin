/*-
 * Copyright (c) 2010, Columbia University
 * All rights reserved.
 *
 * This software was developed by Vasileios P. Kemerlis <vpk@cs.columbia.edu>
 * at Columbia University, New York, NY, USA, in June 2010.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Columbia University nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 09/10/2010:
 * 	r2r_xfer_oplb() was erroneously invoked without a third argument in
 *	MOVSX and MOVZX; proposed fix by Kangkook Jee (jikk@cs.columbia.edu)
 */

/*
 * TODO:
 * 	- optimize rep prefixed MOVS{B, W, D}
 */

#include <errno.h>
#include <cstring>

#include "pin.H"
#include "libdasm.h"
#include "libdft_api.h"
#include "libdft_core.h"
#include "tagmap.h"
#include "branch_pred.h"


typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
#define __func__ __FUNCTION__
/* thread context */
extern REG	thread_ctx_ptr;
extern FILE *inner_logfile;
extern TLS_KEY trace_tls_key;
INS my_ins;

extern struct moditem mi[200];

extern moditem * hash_mod[0x10000];

extern int totalmods;

extern bool is_inited;

extern bool require_update;


FILE *GetOutputFile(THREADID threadid) {
    return ((thread_local *)PIN_GetThreadData(trace_tls_key, threadid)) -> logfile;
}
//unsigned long getins(){
//    thread_local *templocal = (thread_local *)PIN_GetThreadData(trace_tls_key, PIN_ThreadId());
////    printf("PIN_ThreadID() = %d \t PIN_GetThreadData() = %08X ADDR=%08X\n", PIN_ThreadId(), templocal, templocal -> insaddr);
//    return (templocal -> insaddr);
//}
//char * getbin(){
//    thread_local *templocal = (thread_local *)PIN_GetThreadData(trace_tls_key, PIN_ThreadId());
////    printf("PIN_ThreadID() = %d \t PIN_GetThreadData() = %08X ADDR=%08X\n", PIN_ThreadId(), templocal, templocal -> insaddr);
////    for(int i = 0; i < 8; i ++){printf("%02X ", templocal->binary[i]); printf("\n");}
//    return templocal -> binary;
//}
//void setins(INS ins){
//    thread_local *t_local = (thread_local * )PIN_GetThreadData(trace_tls_key, PIN_ThreadId());
//    t_local->insaddr = INS_Address(ins);
//    memcpy(t_local->binary, (unsigned char *)t_local->insaddr, 16);
//    //for(int i = 0; i < 8; i ++){printf("%c ", t_local->binary[i]); printf("\n");}
//    PIN_SetThreadData(trace_tls_key, t_local, PIN_ThreadId());
//}
//void clrins(){
//    thread_local *t_local = (thread_local * )PIN_GetThreadData(trace_tls_key, PIN_ThreadId());
//    t_local->insaddr = 0;
//    memset(t_local->binary,0, 16);
//    //for(int i = 0; i < 8; i ++){printf("%c ", t_local->binary[i]); printf("\n");}
//    PIN_SetThreadData(trace_tls_key, t_local, PIN_ThreadId());
//}
#define KEYDFTTRACE
#ifdef KEYDFTTRACE
#define OUTADDR(addr, value) \
    fprintf(GetOutputFile(), "%s\t%08X\n", addr, value);
#define KEYTRACE0 \
    {\
        thread_local *t_local = ((thread_local *)PIN_GetThreadData(trace_tls_key, threadid));\
        int i, length = t_local->callstack.size();\
        FILE* outputfile = t_local->logfile;\
        for(i = length - 1; i >= 0; i --){\
            if(t_local->callstack[i].is_outputted() == false){\
                moditem *entry = hash_mod[t_local->callstack[i].addr >> 16]; \
                if(entry == NULL) require_update = 1; \
                INSTRUCTION inst; \
                char buff[100]; \
                get_instruction(&inst, (BYTE *)t_local->callstack[i].addr, MODE_32); \
                get_instruction_string(&inst, FORMAT_INTEL, 0, buff, 100);\
                fprintf(outputfile, "[t] %s\t%08X\t%s\n",entry->name, t_local->callstack[i].addr, buff);\
                t_local->callstack[i].set_outputted();\
                t_local->callstack[i].set_tainted();\
            }\
        }\
        moditem *entry = hash_mod[ip1>>16]; \
        if(entry == NULL) require_update = 1; \
        INSTRUCTION inst; \
        char buff[100]; \
        get_instruction(&inst, (BYTE *)ip1, MODE_32); \
        get_instruction_string(&inst, FORMAT_INTEL, 0, buff, 100);\
        fprintf(GetOutputFile(threadid), "[t] %s\t%08X\t%s\n",entry->name, ip1, buff);\
        fflush(GetOutputFile(threadid));\
    }

#define KEYTRACE \
    if(src_tag)KEYTRACE0;
#define KEYTRACE1 \
    if(thread_ctx->vcpu.gpr[dst])KEYTRACE0;
#define KEYTRACE2 \
    if(thread_ctx->vcpu.gpr[src])KEYTRACE0;
#define KEYTRACE3 \
    if(tmp_tag)KEYTRACE0;
#else
#define KEYTRACE
#define KEYTRACE0
#define KEYTRACE1
#define KEYTRACE2
#define KEYTRACE3
#define OUTADDR(addr, value)
#endif

//#define FULLDFTTRACE

#ifdef FULLDFTTRACE
#define TRACEOUT \
    fprintf(inner_logfile, "%08X\t%s\n", INS_Address(ins), INS_Disassemble(ins).c_str());
#else
#define TRACEOUT
#endif




/* tagmap */
extern uint8_t	*bitmap;

/* fast tag extension (helper); [0] -> 0, [1] -> VCPU_MASK16 */
static uint32_t	MAP_8L_16[] = {0, VCPU_MASK16};

/* fast tag extension (helper); [0] -> 0, [2] -> VCPU_MASK16 */
static uint32_t	MAP_8H_16[] = {0, 0, VCPU_MASK16};

/* fast tag extension (helper); [0] -> 0, [1] -> VCPU_MASK32 */
static uint32_t	MAP_8L_32[] = {0, VCPU_MASK32};

/* fast tag extension (helper); [0] -> 0, [2] -> VCPU_MASK32 */
static uint32_t	MAP_8H_32[] = {0, 0, VCPU_MASK32};
/*
 * tag checking (analysis function)
 *
 * extend the tag as follows: t[upper(eax)] = t[ax]
 *
 * NOTE: special case for the CWDE instruction
 *
 * @thread_ctx:	the thread context
 */
static void PIN_FAST_ANALYSIS_CALL
check_register_32bit(thread_ctx_t *thread_ctx, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src];
	if(src_tag)
        KEYTRACE0;
}
static void PIN_FAST_ANALYSIS_CALL
check_register_16bit(thread_ctx_t *thread_ctx, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src] & VCPU_MASK16;
	if(src_tag)
        KEYTRACE0;
}
static void PIN_FAST_ANALYSIS_CALL
check_memory_32bit(thread_ctx_t *thread_ctx, ADDRINT addr, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = (bitmap[VIRT2BYTE(addr)] >> VIRT2BIT(addr));
	if(src_tag)
        KEYTRACE0;
}
static void PIN_FAST_ANALYSIS_CALL
check_memory_16bit(thread_ctx_t *thread_ctx, ADDRINT addr, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = (bitmap[VIRT2BYTE(addr)] >> VIRT2BIT(addr)) & VCPU_MASK16;
	if(src_tag)
        KEYTRACE0;
}

//(AFUNPTR)trace_call,
//IARG_FAST_ANALYSIS_CALL,
//IARG_REG_VALUE, thread_ctx_ptr,
//IARG_THREAD_ID,
//IARG_INST_PTR,
//IARG_END);
static void PIN_FAST_ANALYSIS_CALL
trace_call(CONTEXT *ctxt, THREADID threadid, ADDRINT instaddr){
    thread_local *t_local = ((thread_local *)PIN_GetThreadData(trace_tls_key, threadid));
    t_local->push_a_call(instaddr);
}
static void PIN_FAST_ANALYSIS_CALL
trace_ret(CONTEXT *ctxt, THREADID threadid, ADDRINT instaddr){
    thread_local *t_local = ((thread_local *)PIN_GetThreadData(trace_tls_key, threadid));
    FILE *outputfile = t_local -> logfile;

    if(t_local->is_current_tainted()){
        moditem *entry = hash_mod[instaddr>>16];
        if(entry == NULL) require_update = 1;
        INSTRUCTION inst;
        char buff[100];
        get_instruction(&inst, (BYTE *)instaddr, MODE_32);
        get_instruction_string(&inst, FORMAT_INTEL, 0, buff, 100);
        fprintf(outputfile, "[n] %s\t%08X\t%s\n",entry->name, instaddr, buff);
        fflush(outputfile);
    }
    if(t_local->callstack.size() > 0)
        t_local->pop_a_call();
}
/*
 * tag propagation (analysis function)
 *
 * extend the tag as follows: t[upper(eax)] = t[ax]
 *
 * NOTE: special case for the CWDE instruction
 *
 * @thread_ctx:	the thread context
 */
static void PIN_FAST_ANALYSIS_CALL
_cwde(thread_ctx_t *thread_ctx, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[7] & VCPU_MASK16;
    KEYTRACE;
	/* extension; 16-bit to 32-bit */
	src_tag |= (src_tag << 2);

	/* update the destination */
	thread_ctx->vcpu.gpr[7] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 16-bit
 * register and an 8-bit register as t[dst] = t[upper(src)]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_r2r_opwb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1);
    if(MAP_8H_16[src_tag])
        KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) | MAP_8H_16[src_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 16-bit
 * register and an 8-bit register as t[dst] = t[lower(src)]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_r2r_opwb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		thread_ctx->vcpu.gpr[src] & VCPU_MASK8;
    if(MAP_8L_16[src_tag])
        KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) | MAP_8L_16[src_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and an upper 8-bit register as t[dst] = t[upper(src)]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_r2r_oplb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1);
    if(MAP_8H_32[src_tag])
        KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = MAP_8H_32[src_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and a lower 8-bit register as t[dst] = t[lower(src)]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_r2r_oplb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src] & VCPU_MASK8;
    if(MAP_8L_32[src_tag])
        KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = MAP_8L_32[src_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and a 16-bit register as t[dst] = t[src]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_r2r_oplw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src] & VCPU_MASK16;
    KEYTRACE;
	/* extension; 16-bit to 32-bit */
	src_tag |= (src_tag << 2);

	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 16-bit
 * register and an 8-bit memory location as
 * t[dst] = t[src]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_m2r_opwb(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		(bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8;
    if(MAP_8H_16[src_tag])
        KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) | MAP_8L_16[src_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit
 * register and an 8-bit memory location as
 * t[dst] = t[src]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_m2r_oplb(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		(bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8;
    if(MAP_8L_32[src_tag])
        KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = MAP_8L_32[src_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and a 16-bit register as t[dst] = t[src]
 *
 * NOTE: special case for MOVSX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movsx_m2r_oplw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16);
    KEYTRACE;
	/* extension; 16-bit to 32-bit */
	src_tag |= (src_tag << 2);

	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 16-bit
 * register and an 8-bit register as t[dst] = t[upper(src)]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_r2r_opwb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		(thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1;
    KEYTRACE;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) | src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 16-bit
 * register and an 8-bit register as t[dst] = t[lower(src)]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_r2r_opwb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		thread_ctx->vcpu.gpr[src] & VCPU_MASK8;
    KEYTRACE;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) | src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and an upper 8-bit register as t[dst] = t[upper(src)]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_r2r_oplb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		(thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1;
    KEYTRACE;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and a lower 8-bit register as t[dst] = t[lower(src)]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_r2r_oplb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src] & VCPU_MASK8;
    KEYTRACE;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and a 16-bit register as t[dst] = t[src]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_r2r_oplw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag = thread_ctx->vcpu.gpr[src] & VCPU_MASK16;
    KEYTRACE;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 16-bit
 * register and an 8-bit memory location as
 * t[dst] = t[src]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_m2r_opwb(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		(bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8;
    KEYTRACE;
 //   KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) | src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit
 * register and an 8-bit memory location as
 * t[dst] = t[src]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_m2r_oplb(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		(bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8;
    KEYTRACE;
 //   KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate and extend tag between a 32-bit register
 * and a 16-bit register as t[dst] = t[src]
 *
 * NOTE: special case for MOVZX instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_movzx_m2r_oplw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t src_tag =
		((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16);
    KEYTRACE;
 //   KEYTRACE0;
	/* update the destination (xfer) */
	thread_ctx->vcpu.gpr[dst] = src_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 32-bit
 * registers as t[EAX] = t[src]; return
 * the result of EAX == src and also
 * store the original tag value of
 * EAX in the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst_val:	EAX register value
 * @src:	source register index (VCPU)
 * @src_val:	source register value
 */
static ADDRINT PIN_FAST_ANALYSIS_CALL
_cmpxchg_r2r_opl_fast(thread_ctx_t *thread_ctx, uint32_t dst_val, uint32_t src,
							uint32_t src_val, THREADID threadid, ADDRINT ip1)
{
	/* save the tag value of dst in the scratch register */
	thread_ctx->vcpu.gpr[8] =
		thread_ctx->vcpu.gpr[7];

    if(thread_ctx->vcpu.gpr[src] | thread_ctx->vcpu.gpr[7])
        KEYTRACE0;

	/* update */
	thread_ctx->vcpu.gpr[7] =
		thread_ctx->vcpu.gpr[src];


	/* compare the dst and src values */
	return (dst_val == src_val);
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 32-bit
 * registers as t[dst] = t[src]; restore the
 * value of EAX from the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_cmpxchg_r2r_opl_slow(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* restore the tag value from the scratch register */
	thread_ctx->vcpu.gpr[7] =
		thread_ctx->vcpu.gpr[8];

    if(thread_ctx->vcpu.gpr[dst] | thread_ctx->vcpu.gpr[7])
        KEYTRACE0;

	/* update */
	thread_ctx->vcpu.gpr[dst] =
		thread_ctx->vcpu.gpr[src];
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 16-bit
 * registers as t[AX] = t[src]; return
 * the result of AX == src and also
 * store the original tag value of
 * AX in the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst_val:	AX register value
 * @src:	source register index (VCPU)
 * @src_val:	source register value
 */
static ADDRINT PIN_FAST_ANALYSIS_CALL
_cmpxchg_r2r_opw_fast(thread_ctx_t *thread_ctx, uint16_t dst_val, uint32_t src,
						uint16_t src_val, THREADID threadid, ADDRINT ip1)
{
	/* save the tag value of dst in the scratch register */
	thread_ctx->vcpu.gpr[8] =
		thread_ctx->vcpu.gpr[7];

	/* update */
	thread_ctx->vcpu.gpr[7] =
		(thread_ctx->vcpu.gpr[7] & ~VCPU_MASK16) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK16);

	/* compare the dst and src values */
    if(thread_ctx->vcpu.gpr[src] & VCPU_MASK16)
        KEYTRACE0;

	return (dst_val == src_val);
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 16-bit
 * registers as t[dst] = t[src]; restore the
 * value of AX from the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_cmpxchg_r2r_opw_slow(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* restore the tag value from the scratch register */
	thread_ctx->vcpu.gpr[7] =
		thread_ctx->vcpu.gpr[8];

	/* update */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK16);
    if(thread_ctx->vcpu.gpr[src] & VCPU_MASK16)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location
 * as t[EAX] = t[src]; return the result
 * of EAX == src and also store the
 * original tag value of EAX in
 * the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst_val:	destination register value
 * @src:	source memory address
 */
static ADDRINT PIN_FAST_ANALYSIS_CALL
_cmpxchg_m2r_opl_fast(thread_ctx_t *thread_ctx, uint32_t dst_val, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* save the tag value of dst in the scratch register */
	thread_ctx->vcpu.gpr[8] =
		thread_ctx->vcpu.gpr[7];

	/* update */
	thread_ctx->vcpu.gpr[7] =
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) & VCPU_MASK32;

	/* compare the dst and src values; the original values the tag bits */

//	KEYTRACE2;
    if(thread_ctx->vcpu.gpr[7] | thread_ctx->vcpu.gpr[8])
        KEYTRACE0;
	return (dst_val == *(uint32_t *)src);
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location
 * as t[dst] = t[src]; restore the value
 * of EAX from the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_cmpxchg_r2m_opl_slow(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* restore the tag value from the scratch register */
	thread_ctx->vcpu.gpr[7] =
		thread_ctx->vcpu.gpr[8];

	/* update */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK32) <<
		VIRT2BIT(dst));
    if(((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK32) <<
		VIRT2BIT(dst)))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location
 * as t[AX] = t[src]; return the result
 * of AX == src and also store the
 * original tag value of AX in
 * the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @dst_val:	destination register value
 * @src:	source memory address
 */
static ADDRINT PIN_FAST_ANALYSIS_CALL
_cmpxchg_m2r_opw_fast(thread_ctx_t *thread_ctx, uint16_t dst_val, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* save the tag value of dst in the scratch register */
	thread_ctx->vcpu.gpr[8] =
		thread_ctx->vcpu.gpr[7];

    if(((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16))
		KEYTRACE0;

	/* update */
	thread_ctx->vcpu.gpr[7] =
		(thread_ctx->vcpu.gpr[7] & ~VCPU_MASK16) |
		((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16);

	/* compare the dst and src values; the original values the tag bits */
	return (dst_val == *(uint16_t *)src);
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location
 * as t[dst] = t[src]; restore the value
 * of AX from the scratch register
 *
 * NOTE: special case for the CMPXCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 * @res:	restore register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_cmpxchg_r2m_opw_slow(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* restore the tag value from the scratch register */
	thread_ctx->vcpu.gpr[7] =
		thread_ctx->vcpu.gpr[8];

	/* update */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK16) <<
		VIRT2BIT(dst));
    if(((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK16) <<
		VIRT2BIT(dst)))
		KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] = t[lower(src)]
 * and t[lower(src)] = t[upper(dst)]
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_r2r_opb_ul(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1);
    KEYTRACE3;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		 (thread_ctx->vcpu.gpr[dst] & ~(VCPU_MASK8 << 1)) |
		 ((thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << 1);

	thread_ctx->vcpu.gpr[src] =
		 (thread_ctx->vcpu.gpr[src] & ~VCPU_MASK8) | (tmp_tag >> 1);
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] = t[upper(src)]
 * and t[upper(src)] = t[lower(dst)]
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_r2r_opb_lu(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK8;
    KEYTRACE3;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK8) |
		((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1);

	thread_ctx->vcpu.gpr[src] =
	 (thread_ctx->vcpu.gpr[src] & ~(VCPU_MASK8 << 1)) | (tmp_tag << 1);
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] = t[upper(src)]
 * and t[upper(src)] = t[upper(dst)]
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_r2r_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1);
    KEYTRACE3;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~(VCPU_MASK8 << 1)) |
		(thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1));

	thread_ctx->vcpu.gpr[src] =
		(thread_ctx->vcpu.gpr[src] & ~(VCPU_MASK8 << 1)) | tmp_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] = t[lower(src)]
 * and t[lower(src)] = t[lower(dst)]
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_r2r_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK8;
    KEYTRACE3;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK8) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK8);

	thread_ctx->vcpu.gpr[src] =
		(thread_ctx->vcpu.gpr[src] & ~VCPU_MASK8) | tmp_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 16-bit
 * registers as t[dst] = t[src]
 * and t[src] = t[dst]
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_r2r_opw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK16;
    KEYTRACE3;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK16);

	thread_ctx->vcpu.gpr[src] =
		(thread_ctx->vcpu.gpr[src] & ~VCPU_MASK16) | tmp_tag;
    KEYTRACE2;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an upper 8-bit
 * register and a memory location as
 * t[dst] = t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_m2r_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1);

	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~(VCPU_MASK8 << 1)) |
		(((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) << 1) &
		(VCPU_MASK8 << 1));

	bitmap[VIRT2BYTE(dst)] =
		(bitmap[VIRT2BYTE(dst)] & ~(BYTE_MASK << VIRT2BIT(dst))) |
		((tmp_tag >> 1) << VIRT2BIT(dst));

    if((((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) << 1) &
		(VCPU_MASK8 << 1)) | tmp_tag)
		KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a lower 8-bit
 * register and a memory location as
 * t[dst] = t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_m2r_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK8;

	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK8) |
		((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8);

	bitmap[VIRT2BYTE(dst)] =
		(bitmap[VIRT2BYTE(dst)] & ~(BYTE_MASK << VIRT2BIT(dst))) |
		(tmp_tag << VIRT2BIT(dst));

    if(((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8) | tmp_tag)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location as
 * t[dst] = t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_m2r_opw(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK16;

	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) |
		((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16);

	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(tmp_tag) << VIRT2BIT(dst));

    if(((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16) | tmp_tag)
		KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location as
 * t[dst] = t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XCHG instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xchg_m2r_opl(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst];

	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK32;

	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(tmp_tag) << VIRT2BIT(dst));

    if(tmp_tag | (*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] |= t[lower(src)]
 * and t[lower(src)] = t[upper(dst)]
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_r2r_opb_ul(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1);

	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		 (thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1)) |
		 ((thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << 1);

	thread_ctx->vcpu.gpr[src] =
		 (thread_ctx->vcpu.gpr[src] & ~VCPU_MASK8) | (tmp_tag >> 1);

    if(thread_ctx->vcpu.gpr[src] | tmp_tag)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] |= t[upper(src)]
 * and t[upper(src)] = t[lower(dst)]
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_r2r_opb_lu(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK8;

	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & VCPU_MASK8) |
		((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1);

	thread_ctx->vcpu.gpr[src] =
	 (thread_ctx->vcpu.gpr[src] & ~(VCPU_MASK8 << 1)) | (tmp_tag << 1);

	 if(tmp_tag | ((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] |= t[upper(src)]
 * and t[upper(src)] = t[upper(dst)]
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_r2r_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1);
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1)) |
		(thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1));

	thread_ctx->vcpu.gpr[src] =
		(thread_ctx->vcpu.gpr[src] & ~(VCPU_MASK8 << 1)) | tmp_tag;

    if(tmp_tag | (thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] |= t[lower(src)]
 * and t[lower(src)] = t[lower(dst)]
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_r2r_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK8;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & VCPU_MASK8) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK8);

	thread_ctx->vcpu.gpr[src] =
		(thread_ctx->vcpu.gpr[src] & ~VCPU_MASK8) | tmp_tag;

    if(tmp_tag | (thread_ctx->vcpu.gpr[src] & VCPU_MASK8))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 16-bit
 * registers as t[dst] |= t[src]
 * and t[src] = t[dst]
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_r2r_opw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK16;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & VCPU_MASK16) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK16);

	thread_ctx->vcpu.gpr[src] =
		(thread_ctx->vcpu.gpr[src] & ~VCPU_MASK16) | tmp_tag;
    if(tmp_tag | (thread_ctx->vcpu.gpr[src] & VCPU_MASK16))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an upper 8-bit
 * register and a memory location as
 * t[dst] |= t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_m2r_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1);
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & (VCPU_MASK8 << 1)) |
		(((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) << 1) &
		(VCPU_MASK8 << 1));

	bitmap[VIRT2BYTE(dst)] =
		(bitmap[VIRT2BYTE(dst)] & ~(BYTE_MASK << VIRT2BIT(dst))) |
		((tmp_tag >> 1) << VIRT2BIT(dst));
    if(tmp_tag | (((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) << 1) &
		(VCPU_MASK8 << 1)))
		KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a lower 8-bit
 * register and a memory location as
 * t[dst] |= t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_m2r_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK8;

	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & VCPU_MASK8) |
		((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8);

	bitmap[VIRT2BYTE(dst)] =
		(bitmap[VIRT2BYTE(dst)] & ~(BYTE_MASK << VIRT2BIT(dst))) |
		(tmp_tag << VIRT2BIT(dst));

    if(tmp_tag | ((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8))
        KEYTRACE0;

}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location as
 * t[dst] |= t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_m2r_opw(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst] & VCPU_MASK16;
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & VCPU_MASK16) |
		((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16);

	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(tmp_tag) < VIRT2BIT(dst));
    if(tmp_tag | ((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16))
		KEYTRACE0;

}


/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location as
 * t[dst] |= t[src] and t[src] = t[dst]
 * (dst is a register)
 *
 * NOTE: special case for the XADD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
_xadd_m2r_opl(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[dst];
	/* swap */
	thread_ctx->vcpu.gpr[dst] =
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK32;

	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & (LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(tmp_tag) << VIRT2BIT(dst));
    if(tmp_tag | (*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK32)
		KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between three 16-bit
 * registers as t[dst] = t[base] | t[index]
 *
 * NOTE: special case for the LEA instruction
 *
 * @thread_ctx: the thread context
 * @dst:        destination register
 * @base:       base register
 * @index:      index register
 */
static void PIN_FAST_ANALYSIS_CALL
_lea_r2r_opw(thread_ctx_t *thread_ctx,
		uint32_t dst,
		uint32_t base,
		uint32_t index, THREADID threadid, ADDRINT ip1)
{
	/* update the destination */
	thread_ctx->vcpu.gpr[dst] =
		((thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) |
		(thread_ctx->vcpu.gpr[base] & VCPU_MASK16) |
		(thread_ctx->vcpu.gpr[index] & VCPU_MASK16));
    KEYTRACE1;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between three 32-bit
 * registers as t[dst] = t[base] | t[index]
 *
 * NOTE: special case for the LEA instruction
 *
 * @thread_ctx: the thread context
 * @dst:        destination register
 * @base:       base register
 * @index:      index register
 */
static void PIN_FAST_ANALYSIS_CALL
_lea_r2r_opl(thread_ctx_t *thread_ctx,
		uint32_t dst,
		uint32_t base,
		uint32_t index, THREADID threadid, ADDRINT ip1)
{
	/* update the destination */
	thread_ctx->vcpu.gpr[dst] =
		thread_ctx->vcpu.gpr[base] | thread_ctx->vcpu.gpr[index];
    KEYTRACE1;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag among three 8-bit registers as t[dst] |= t[upper(src)];
 * dst is AX, whereas src is an upper 8-bit register (e.g., CH, BH, ...)
 *
 * NOTE: special case for DIV and IDIV instructions
 *
 * @thread_ctx:	the thread context
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_ternary_opb_u(thread_ctx_t *thread_ctx, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1);
    KEYTRACE3;
	/* update the destination (ternary) */
	thread_ctx->vcpu.gpr[7] |= MAP_8H_16[tmp_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag among three 8-bit registers as t[dst] |= t[lower(src)];
 * dst is AX, whereas src is a lower 8-bit register (e.g., CL, BL, ...)
 *
 * NOTE: special case for DIV and IDIV instructions
 *
 * @thread_ctx:	the thread context
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_ternary_opb_l(thread_ctx_t *thread_ctx, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[src] & VCPU_MASK8;
    KEYTRACE3;
	/* update the destination (ternary) */
	thread_ctx->vcpu.gpr[7] |= MAP_8L_16[tmp_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between among three 16-bit
 * registers as t[dst1] |= t[src] and t[dst2] |= t[src];
 * dst1 is DX, dst2 is AX, and src is a 16-bit register
 * (e.g., CX, BX, ...)
 *
 * NOTE: special case for DIV and IDIV instructions
 *
 * @thread_ctx:	the thread context
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_ternary_opw(thread_ctx_t *thread_ctx, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag = thread_ctx->vcpu.gpr[src] & VCPU_MASK16;
    KEYTRACE3;
	/* update the destinations */
	thread_ctx->vcpu.gpr[5] |= tmp_tag;
	thread_ctx->vcpu.gpr[7] |= tmp_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between among three 32-bit
 * registers as t[dst1] |= t[src] and t[dst2] |= t[src];
 * dst1 is EDX, dst2 is EAX, and src is a 32-bit register
 * (e.g., ECX, EBX, ...)
 *
 * NOTE: special case for DIV and IDIV instructions
 *
 * @thread_ctx:	the thread context
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_ternary_opl(thread_ctx_t *thread_ctx, uint32_t src, THREADID threadid, ADDRINT ip1)
{
    KEYTRACE2;
	/* update the destinations */
	thread_ctx->vcpu.gpr[5] |= thread_ctx->vcpu.gpr[src];
	thread_ctx->vcpu.gpr[7] |= thread_ctx->vcpu.gpr[src];
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag among two 8-bit registers
 * and an 8-bit memory location as t[dst] |= t[src];
 * dst is AX, whereas src is an 8-bit memory location
 *
 * NOTE: special case for DIV and IDIV instructions
 *
 * @thread_ctx:	the thread context
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_ternary_opb(thread_ctx_t *thread_ctx, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag =
		(bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8;
    KEYTRACE3;
	/* update the destination (ternary) */
	thread_ctx->vcpu.gpr[7] |= MAP_8L_16[tmp_tag];
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag among two 16-bit registers
 * and a 16-bit memory address as
 * t[dst1] |= t[src] and t[dst1] |= t[src];
 *
 * dst1 is DX, dst2 is AX, and src is a 16-bit
 * memory location
 *
 * NOTE: special case for DIV and IDIV instructions
 *
 * @thread_ctx:	the thread context
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_ternary_opw(thread_ctx_t *thread_ctx, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag =
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16;
    KEYTRACE3;
	/* update the destinations */
	thread_ctx->vcpu.gpr[5] |= tmp_tag;
	thread_ctx->vcpu.gpr[7] |= tmp_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag among two 32-bit
 * registers and a 32-bit memory as
 * t[dst1] |= t[src] and t[dst2] |= t[src];
 * dst1 is EDX, dst2 is EAX, and src is a 32-bit
 * memory location
 *
 * NOTE: special case for DIV and IDIV instructions
 *
 * @thread_ctx:	the thread context
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_ternary_opl(thread_ctx_t *thread_ctx, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* temporary tag value */
	size_t tmp_tag =
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK32;
    KEYTRACE3;
	/* update the destinations */
	thread_ctx->vcpu.gpr[5] |= tmp_tag;
	thread_ctx->vcpu.gpr[7] |= tmp_tag;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] |= t[lower(src)];
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_binary_opb_ul(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << 1;
    if((thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << 1)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] |= t[upper(src)];
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_binary_opb_lu(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		(thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1;
    if((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] |= t[upper(src)]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_binary_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1);
    if(thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] |= t[lower(src)]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_binary_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		thread_ctx->vcpu.gpr[src] & VCPU_MASK8;
    if(thread_ctx->vcpu.gpr[src] & VCPU_MASK8)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 16-bit registers
 * as t[dst] |= t[src]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_binary_opw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		thread_ctx->vcpu.gpr[src] & VCPU_MASK16;
    if(thread_ctx->vcpu.gpr[src] & VCPU_MASK16)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 32-bit
 * registers as t[dst] |= t[src]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_binary_opl(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |= thread_ctx->vcpu.gpr[src];
	KEYTRACE2;
 }

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an 8-bit
 * register and a memory location as
 * t[upper(dst)] |= t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_binary_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8) << 1;
    if(((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8) << 1){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an 8-bit
 * register and a memory location as
 * t[lower(dst)] |= t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_binary_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		(bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8;
    if((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8){
            KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location as
 * t[dst] |= t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_binary_opw(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16;
    if((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) & VCPU_MASK16){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location as
 * t[dst] |= t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_binary_opl(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] |=
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) & VCPU_MASK32;
    if((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) & VCPU_MASK32){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an 8-bit
 * register and a memory location as
 * t[dst] |= t[upper(src)] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_binary_opb_u(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	bitmap[VIRT2BYTE(dst)] |=
		((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1)<< VIRT2BIT(dst);
    if(((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1)<< VIRT2BIT(dst)){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an 8-bit
 * register and a memory location as
 * t[dst] |= t[lower(src)] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_binary_opb_l(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	bitmap[VIRT2BYTE(dst)] |=
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << VIRT2BIT(dst);
    if((thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << VIRT2BIT(dst)){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location as
 * t[dst] |= t[src] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_binary_opw(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) |=
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK16) << VIRT2BIT(dst);
    if((thread_ctx->vcpu.gpr[src] & VCPU_MASK16) << VIRT2BIT(dst)){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location as
 * t[dst] |= t[src] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_binary_opl(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) |=
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK32) << VIRT2BIT(dst);
    if((thread_ctx->vcpu.gpr[src] & VCPU_MASK32) << VIRT2BIT(dst)){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * clear the tag of EAX, EBX, ECX, EDX
 *
 * @thread_ctx:	the thread context
 * @reg:	register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r_clrl4(thread_ctx_t *thread_ctx)
{
	thread_ctx->vcpu.gpr[4] = 0;
	thread_ctx->vcpu.gpr[5] = 0;
	thread_ctx->vcpu.gpr[6] = 0;
	thread_ctx->vcpu.gpr[7] = 0;
}

/*
 * tag propagation (analysis function)
 *
 * clear the tag of EAX, EDX
 *
 * @thread_ctx:	the thread context
 * @reg:	register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r_clrl2(thread_ctx_t *thread_ctx)
{
	thread_ctx->vcpu.gpr[5] = 0;
	thread_ctx->vcpu.gpr[7] = 0;
}

/*
 * tag propagation (analysis function)
 *
 * clear the tag of a 32-bit register
 *
 * @thread_ctx:	the thread context
 * @reg:	register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r_clrl(thread_ctx_t *thread_ctx, uint32_t reg)
{
	thread_ctx->vcpu.gpr[reg] = 0;
}

/*
 * tag propagation (analysis function)
 *
 * clear the tag of a 16-bit register
 *
 * @thread_ctx:	the thread context
 * @reg:	register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r_clrw(thread_ctx_t *thread_ctx, uint32_t reg)
{
	thread_ctx->vcpu.gpr[reg] &= ~VCPU_MASK16;
}

/*
 * tag propagation (analysis function)
 *
 * clear the tag of an upper 8-bit register
 *
 * @thread_ctx:	the thread context
 * @reg:	register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r_clrb_u(thread_ctx_t *thread_ctx, uint32_t reg)
{
	thread_ctx->vcpu.gpr[reg] &= ~(VCPU_MASK8 << 1);
}

/*
 * tag propagation (analysis function)
 *
 * clear the tag of a lower 8-bit register
 *
 * @thread_ctx:	the thread context
 * @reg:	register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r_clrb_l(thread_ctx_t *thread_ctx, uint32_t reg)
{
	thread_ctx->vcpu.gpr[reg] &= ~VCPU_MASK8;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] = t[lower(src)]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_xfer_opb_ul(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	 thread_ctx->vcpu.gpr[dst] =
		 (thread_ctx->vcpu.gpr[dst] & ~(VCPU_MASK8 << 1)) |	 ((thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << 1);
    if((thread_ctx->vcpu.gpr[src] & VCPU_MASK8))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] = t[upper(src)];
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_xfer_opb_lu(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK8) |
		((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1);
    if((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[upper(dst)] = t[upper(src)]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_xfer_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~(VCPU_MASK8 << 1)) |(thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1));
	if((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)))
        KEYTRACE0;

}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * registers as t[lower(dst)] = t[lower(src)]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_xfer_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK8) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK8);
    if(thread_ctx->vcpu.gpr[src] & VCPU_MASK8)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 16-bit
 * registers as t[dst] = t[src]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_xfer_opw(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) |
		(thread_ctx->vcpu.gpr[src] & VCPU_MASK16);
    if(thread_ctx->vcpu.gpr[src] & VCPU_MASK16)
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 32-bit
 * registers as t[dst] = t[src]
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2r_xfer_opl(thread_ctx_t *thread_ctx, uint32_t dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		thread_ctx->vcpu.gpr[src];
    KEYTRACE2;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an upper 8-bit
 * register and a memory location as
 * t[dst] = t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_xfer_opb_u(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~(VCPU_MASK8 << 1)) |
		(((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) << 1) &
		(VCPU_MASK8 << 1));
    if(((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) << 1) &
		(VCPU_MASK8 << 1))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a lower 8-bit
 * register and a memory location as
 * t[dst] = t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_xfer_opb_l(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK8) |
		((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8);
    if(((bitmap[VIRT2BYTE(src)] >> VIRT2BIT(src)) & VCPU_MASK8))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location as
 * t[dst] = t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_xfer_opw(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(thread_ctx->vcpu.gpr[dst] & ~VCPU_MASK16) |
		((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16);
    if(((*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK16))
		KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location as
 * t[dst] = t[src] (dst is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination register index (VCPU)
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_xfer_opl(thread_ctx_t *thread_ctx, uint32_t dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	thread_ctx->vcpu.gpr[dst] =
		(*((uint16_t *)(bitmap + VIRT2BYTE(src))) >> VIRT2BIT(src)) &
		VCPU_MASK32;
    KEYTRACE1;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an 8-bit
 * register and a n-memory locations as
 * t[dst] = t[src]; src is AL
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @count:	memory bytes
 * @eflags:	the value of the EFLAGS register
 *
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_xfer_opbn(thread_ctx_t *thread_ctx,
		ADDRINT dst,
		ADDRINT count,
		ADDRINT eflags, THREADID threadid, ADDRINT ip1)
{
	if (likely(EFLAGS_DF(eflags) == 0)) {
		/* EFLAGS.DF = 0 */

		/* the source register is taged */
		if (thread_ctx->vcpu.gpr[7] & VCPU_MASK8){
			tagmap_setn(dst, count);
			KEYTRACE0;
		}
		/* the source register is clear */
		else
			tagmap_clrn(dst, count);
	}
	else {
		/* EFLAGS.DF = 1 */

		/* the source register is taged */
		if (thread_ctx->vcpu.gpr[7] & VCPU_MASK8){
			tagmap_setn(dst - count + 1, count);
			KEYTRACE0;
		}
		/* the source register is clear */
		else
			tagmap_clrn(dst - count + 1, count);

	}
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an 8-bit
 * register and a memory location as
 * t[dst] = t[upper(src)] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_xfer_opb_u(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	bitmap[VIRT2BYTE(dst)] =
		(bitmap[VIRT2BYTE(dst)] & ~(BYTE_MASK << VIRT2BIT(dst))) |
		(((thread_ctx->vcpu.gpr[src] & (VCPU_MASK8 << 1)) >> 1)
		<< VIRT2BIT(dst));
    if(bitmap[VIRT2BYTE(dst)])
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between an 8-bit
 * register and a memory location as
 * t[dst] = t[lower(src)] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_xfer_opb_l(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	bitmap[VIRT2BYTE(dst)] =
		(bitmap[VIRT2BYTE(dst)] & ~(BYTE_MASK << VIRT2BIT(dst))) |
		((thread_ctx->vcpu.gpr[src] & VCPU_MASK8) << VIRT2BIT(dst));
    if(bitmap[VIRT2BYTE(dst)])
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a n-memory locations as
 * t[dst] = t[src]; src is AX
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @count:	memory words
 * @eflags:	the value of the EFLAGS register
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_xfer_opwn(thread_ctx_t *thread_ctx,
		ADDRINT dst,
		ADDRINT count,
		ADDRINT eflags, THREADID threadid, ADDRINT ip1)
{
	if (likely(EFLAGS_DF(eflags) == 0)) {
		/* EFLAGS.DF = 0 */

		/* the source register is taged */
		if (thread_ctx->vcpu.gpr[7] & VCPU_MASK16){
			tagmap_setn(dst, (count << 1));
			KEYTRACE0;
		}
		/* the source register is clear */
		else
			tagmap_clrn(dst, (count << 1));
	}
	else {
		/* EFLAGS.DF = 1 */

		/* the source register is taged */
		if (thread_ctx->vcpu.gpr[7] & VCPU_MASK16){
			tagmap_setn(dst - (count << 1) + 1, (count << 1));
			KEYTRACE0;
		}
		/* the source register is clear */
		else
			tagmap_clrn(dst - (count << 1) + 1, (count << 1));
	}
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 16-bit
 * register and a memory location as
 * t[dst] = t[src] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_xfer_opw(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK16) <<
		VIRT2BIT(dst));
    if(((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK16) <<
		VIRT2BIT(dst)))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a n-memory locations as
 * t[dst] = t[src]; src is EAX
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 * @count:	memory double words
 * @eflags:	the value of the EFLAGS register
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_xfer_opln(thread_ctx_t *thread_ctx,
		ADDRINT dst,
		ADDRINT count,
		ADDRINT eflags, THREADID threadid, ADDRINT ip1)
{
	if (likely(EFLAGS_DF(eflags) == 0)) {
		/* EFLAGS.DF = 0 */

		/* the source register is taged */
		if (thread_ctx->vcpu.gpr[7]){
			tagmap_setn(dst, (count << 2));
			KEYTRACE0;
		}
		/* the source register is clear */
		else
			tagmap_clrn(dst, (count << 2));
	}
	else {
		/* EFLAGS.DF = 1 */

		/* the source register is taged */
		if (thread_ctx->vcpu.gpr[7]){
			tagmap_setn(dst - (count << 2) + 1, (count << 2));
			KEYTRACE0;
		}
		/* the source register is clear */
		else
			tagmap_clrn(dst - (count << 2) + 1, (count << 2));
	}
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between a 32-bit
 * register and a memory location as
 * t[dst] = t[src] (src is a register)
 *
 * @thread_ctx:	the thread context
 * @dst:	destination memory address
 * @src:	source register index (VCPU)
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_xfer_opl(thread_ctx_t *thread_ctx, ADDRINT dst, uint32_t src, THREADID threadid, ADDRINT ip1)
{
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK32) <<
		VIRT2BIT(dst));
    if(((uint16_t)(thread_ctx->vcpu.gpr[src] & VCPU_MASK32) <<
		VIRT2BIT(dst)))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 16-bit
 * memory locations as t[dst] = t[src]
 *
 * @dst:	destination memory address
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2m_xfer_opw(ADDRINT dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		(((*((uint16_t *)(bitmap + VIRT2BYTE(src)))) >> VIRT2BIT(src))
		& WORD_MASK) << VIRT2BIT(dst);
    if((((*((uint16_t *)(bitmap + VIRT2BYTE(src)))) >> VIRT2BIT(src))
		& WORD_MASK) << VIRT2BIT(dst)){
        KEYTRACE0;
    }
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 8-bit
 * memory locations as t[dst] = t[src]
 *
 * @dst:	destination memory address
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2m_xfer_opb(ADDRINT dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(BYTE_MASK <<  VIRT2BIT(dst))) |
		(((*((uint16_t *)(bitmap + VIRT2BYTE(src)))) >> VIRT2BIT(src))
		& BYTE_MASK) << VIRT2BIT(dst);
    if((((*((uint16_t *)(bitmap + VIRT2BYTE(src)))) >> VIRT2BIT(src))
		& BYTE_MASK) << VIRT2BIT(dst))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * propagate tag between two 32-bit
 * memory locations as t[dst] = t[src]
 *
 * @dst:	destination memory address
 * @src:	source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2m_xfer_opl(ADDRINT dst, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
    //OUTADDR("xfer_opl:src", src);
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		(((*((uint16_t *)(bitmap + VIRT2BYTE(src)))) >> VIRT2BIT(src))
		& LONG_MASK) << VIRT2BIT(dst);
    if((((*((uint16_t *)(bitmap + VIRT2BYTE(src)))) >> VIRT2BIT(src))& LONG_MASK) << VIRT2BIT(dst))
        KEYTRACE0;
}

/*
 * tag propagation (analysis function)
 *
 * instrumentation helper; returns the flag that
 * takes as argument -- seems lame, but it is
 * necessary for aiding conditional analysis to
 * be inlined. Typically used with INS_InsertIfCall()
 * in order to return true (i.e., allow the execution
 * of the function that has been instrumented with
 * INS_InsertThenCall()) only once
 *
 * first_iteration:	flag; indicates whether the rep-prefixed instruction is
 * 			executed for the first time or not
 */
static ADDRINT PIN_FAST_ANALYSIS_CALL
rep_predicate(BOOL first_iteration)
{
	/* return the flag; typically this is true only once */
	return first_iteration;
}

/*
 * tag propagation (analysis function)
 *
 * restore the tag values for all the
 * 16-bit general purpose registers from
 * the memory
 *
 * NOTE: special case for POPA instruction
 *
 * @thread_ctx:	the thread context
 * @src:	the source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_restore_opw(thread_ctx_t *thread_ctx, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* tagmap value */
	uint16_t src_val = *(uint16_t *)(bitmap + VIRT2BYTE(src));

	/* restore DI */
	thread_ctx->vcpu.gpr[0] =
		(thread_ctx->vcpu.gpr[0] & ~VCPU_MASK16) |
		((src_val >> VIRT2BIT(src)) & VCPU_MASK16);

	/* restore SI */
	thread_ctx->vcpu.gpr[1] =
		(thread_ctx->vcpu.gpr[1] & ~VCPU_MASK16) |
		((src_val >> VIRT2BIT(src + 2)) & VCPU_MASK16);

	/* restore BP */
	thread_ctx->vcpu.gpr[2] =
		(thread_ctx->vcpu.gpr[2] & ~VCPU_MASK16) |
		((src_val >> VIRT2BIT(src + 4)) & VCPU_MASK16);

	/* update the tagmap value */
	src	+= 8;
	src_val	= *(uint16_t *)(bitmap + VIRT2BYTE(src));

	/* restore BX */
	thread_ctx->vcpu.gpr[4] =
		(thread_ctx->vcpu.gpr[4] & ~VCPU_MASK16) |
		((src_val >> VIRT2BIT(src)) & VCPU_MASK16);

	/* restore DX */
	thread_ctx->vcpu.gpr[5] =
		(thread_ctx->vcpu.gpr[5] & ~VCPU_MASK16) |
		((src_val >> VIRT2BIT(src + 2)) & VCPU_MASK16);

	/* restore CX */
	thread_ctx->vcpu.gpr[6] =
		(thread_ctx->vcpu.gpr[6] & ~VCPU_MASK16) |
		((src_val >> VIRT2BIT(src + 4)) & VCPU_MASK16);

	/* restore AX */
	thread_ctx->vcpu.gpr[7] =
		(thread_ctx->vcpu.gpr[7] & ~VCPU_MASK16) |
		((src_val >> VIRT2BIT(src + 6)) & VCPU_MASK16);
    unsigned int i;
    for(i = 0; i < 8; i ++){
        if(thread_ctx->vcpu.gpr[i]){
            KEYTRACE0;
            break;
        }
    }
}

/*
 * tag propagation (analysis function)
 *
 * restore the tag values for all the
 * 32-bit general purpose registers from
 * the memory
 *
 * NOTE: special case for POPAD instruction
 *
 * @thread_ctx:	the thread context
 * @src:	the source memory address
 */
static void PIN_FAST_ANALYSIS_CALL
m2r_restore_opl(thread_ctx_t *thread_ctx, ADDRINT src, THREADID threadid, ADDRINT ip1)
{
	/* tagmap value */
	uint16_t src_val = *(uint16_t *)(bitmap + VIRT2BYTE(src));

	/* restore EDI */
	thread_ctx->vcpu.gpr[0] =
		(src_val >> VIRT2BIT(src)) & VCPU_MASK32;

	/* restore ESI */
	thread_ctx->vcpu.gpr[1] =
		(src_val >> VIRT2BIT(src + 4)) & VCPU_MASK32;

	/* update the tagmap value */
	src	+= 8;
	src_val	= *(uint16_t *)(bitmap + VIRT2BYTE(src));

	/* restore EBP */
	thread_ctx->vcpu.gpr[2] =
		(src_val >> VIRT2BIT(src)) & VCPU_MASK32;

	/* update the tagmap value */
	src	+= 8;
	src_val	= *(uint16_t *)(bitmap + VIRT2BYTE(src));

	/* restore EBX */
	thread_ctx->vcpu.gpr[4] =
		(src_val >> VIRT2BIT(src)) & VCPU_MASK32;

	/* restore EDX */
	thread_ctx->vcpu.gpr[5] =
		(src_val >> VIRT2BIT(src + 4)) & VCPU_MASK32;

	/* update the tagmap value */
	src	+= 8;
	src_val	= *(uint16_t *)(bitmap + VIRT2BYTE(src));

	/* restore ECX */
	thread_ctx->vcpu.gpr[6] =
		(src_val >> VIRT2BIT(src)) & VCPU_MASK32;

	/* restore EAX */
	thread_ctx->vcpu.gpr[7] =
		(src_val >> VIRT2BIT(src + 4)) & VCPU_MASK32;
    unsigned int i;
    for(i = 0; i < 8; i ++){
        if(thread_ctx->vcpu.gpr[i]){
            KEYTRACE0;
            break;
        }
    }
}

/*
 * tag propagation (analysis function)
 *
 * save the tag values for all the 16-bit
 * general purpose registers into the memory
 *
 * NOTE: special case for PUSHA instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	the destination memory address
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_save_opw(thread_ctx_t *thread_ctx, ADDRINT dst, THREADID threadid, ADDRINT ip1)
{
	/* save DI */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[0] & VCPU_MASK16) <<
		VIRT2BIT(dst));

	/* update the destination memory */
	dst += 2;

	/* save SI */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[1] & VCPU_MASK16) <<
		VIRT2BIT(dst));

	/* update the destination memory */
	dst += 2;

	/* save BP */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[2] & VCPU_MASK16) <<
		VIRT2BIT(dst));

	/* update the destination memory */
	dst += 2;

	/* save SP */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[3] & VCPU_MASK16) <<
		VIRT2BIT(dst));

	/* update the destination memory */
	dst += 2;

	/* save BX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[4] & VCPU_MASK16) <<
		VIRT2BIT(dst));

	/* update the destination memory */
	dst += 2;

	/* save DX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[5] & VCPU_MASK16) <<
		VIRT2BIT(dst));

	/* update the destination memory */
	dst += 2;

	/* save CX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[6] & VCPU_MASK16) <<
		VIRT2BIT(dst));

	/* update the destination memory */
	dst += 2;

	/* save AX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(WORD_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[7] & VCPU_MASK16) <<
		VIRT2BIT(dst));
    unsigned int i;
    for(i = 0; i < 8; i ++){
        if(thread_ctx->vcpu.gpr[i] & VCPU_MASK16){
            KEYTRACE0;
            break;
        }
    }
}

/*
 * tag propagation (analysis function)
 *
 * save the tag values for all the 32-bit
 * general purpose registers into the memory
 *
 * NOTE: special case for PUSHAD instruction
 *
 * @thread_ctx:	the thread context
 * @dst:	the destination memory address
 */
static void PIN_FAST_ANALYSIS_CALL
r2m_save_opl(thread_ctx_t *thread_ctx, ADDRINT dst, THREADID threadid, ADDRINT ip1)
{
	/* save EDI */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[0] & VCPU_MASK32) <<
		VIRT2BIT(dst));

	/* update the destination memory address */
	dst += 4;

	/* save ESI */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[1] & VCPU_MASK32) <<
		VIRT2BIT(dst));

	/* update the destination memory address */
	dst += 4;

	/* save EBP */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[2] & VCPU_MASK32) <<
		VIRT2BIT(dst));

	/* update the destination memory address */
	dst += 4;

	/* save ESP */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[3] & VCPU_MASK32) <<
		VIRT2BIT(dst));

	/* update the destination memory address */
	dst += 4;

	/* save EBX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[4] & VCPU_MASK32) <<
		VIRT2BIT(dst));

	/* update the destination memory address */
	dst += 4;

	/* save EDX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[5] & VCPU_MASK32) <<
		VIRT2BIT(dst));

	/* update the destination memory address */
	dst += 4;

	/* save ECX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[6] & VCPU_MASK32) <<
		VIRT2BIT(dst));

	/* update the destination memory address */
	dst += 4;

	/* save EAX */
	*((uint16_t *)(bitmap + VIRT2BYTE(dst))) =
		(*((uint16_t *)(bitmap + VIRT2BYTE(dst))) & ~(LONG_MASK <<
							      VIRT2BIT(dst))) |
		((uint16_t)(thread_ctx->vcpu.gpr[7] & VCPU_MASK32) <<
		VIRT2BIT(dst));
    unsigned int i;
    for(i = 0; i < 8; i ++){
        if(thread_ctx->vcpu.gpr[i] & VCPU_MASK32){
            KEYTRACE0;
            break;
        }
    }
}

/*
 * instruction inspection (instrumentation function)
 *
 * analyze every instruction and instrument it
 * for propagating the tag bits accordingly
 *
 * @ins:	the instruction to be instrumented
 */
void
ins_inspect(const INS ins)
{
	/*
	 * temporaries;
	 * source, destination, base, and index registers
	 */
	REG reg_dst, reg_src, reg_base, reg_indx;

	/* use XED to decode the instruction and extract its opcode */
	xed_iclass_enum_t ins_indx = (xed_iclass_enum_t)INS_Opcode(ins);

//    setins(ins);
//    getins();
//    fprintf(stdout, "before: %08X\t%s\n", INS_Address(ins), INS_Disassemble(ins).c_str());
//    fprintf(stdout, "after : %08X\n", getins());
    //printf("after ins_inspect!\n");
	/* sanity check */
	if (unlikely(ins_indx <= XED_ICLASS_INVALID ||
				ins_indx >= XED_ICLASS_LAST)) {
		LOG(string(__func__) + ": unknown opcode (opcode=" + decstr(ins_indx) + ")\n");

		/* done */
		return;
	}
	/* analyze the instruction */
	switch (ins_indx) {
		/* adc */
		case XED_ICLASS_ADC:
		/* add */
		case XED_ICLASS_ADD:
		/* and */
		case XED_ICLASS_AND:
		/* or */
		case XED_ICLASS_OR:
		/* xor */
		case XED_ICLASS_XOR:
		/* sbb */
		case XED_ICLASS_SBB:
		/* sub */
		case XED_ICLASS_SUB:
			/*
			 * the general format of these instructions
			 * is the following: dst {op}= src, where
			 * op can be +, -, &, |, etc. We tag the
			 * destination if the source is also taged
			 * (i.e., t[dst] |= t[src])
			 */
			/* 2nd operand is immediate; do nothing */
			if (INS_OperandIsImmediate(ins, OP_1))
				break;

			/* both operands are registers */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)) {
					/* check for x86 clear register idiom */
					switch (ins_indx) {
						/* xor, sub, sbb */
						case XED_ICLASS_XOR:
						case XED_ICLASS_SUB:
						case XED_ICLASS_SBB:
							/* same dst, src */
							if (reg_dst == reg_src)
							{
								/* clear */
							TRACEOUT; INS_InsertCall(ins,
								IPOINT_BEFORE,
								(AFUNPTR)r_clrl,
							IARG_FAST_ANALYSIS_CALL,
								IARG_REG_VALUE,
								thread_ctx_ptr,
								IARG_UINT32,
							REG32_INDX(reg_dst),
								IARG_END);

								/* done */
								break;
							}
						/* default behavior */
						default:
							/*
							 * propagate the tag
							 * markings accordingly
							 */
							TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)r2r_binary_opl,
							IARG_FAST_ANALYSIS_CALL,
							IARG_REG_VALUE,
							thread_ctx_ptr,
							IARG_UINT32,
							REG32_INDX(reg_dst),
							IARG_UINT32,
							REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);
					}
				}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)) {
					/* check for x86 clear register idiom */
					switch (ins_indx) {
						/* xor, sub, sbb */
						case XED_ICLASS_XOR:
						case XED_ICLASS_SUB:
						case XED_ICLASS_SBB:
							/* same dst, src */
							if (reg_dst == reg_src)
							{
								/* clear */
							TRACEOUT; INS_InsertCall(ins,
								IPOINT_BEFORE,
								(AFUNPTR)r_clrw,
							IARG_FAST_ANALYSIS_CALL,
								IARG_REG_VALUE,
								thread_ctx_ptr,
								IARG_UINT32,
							REG16_INDX(reg_dst),
								IARG_END);

								/* done */
								break;
							}
						/* default behavior */
						default:
						/* propagate tags accordingly */
							TRACEOUT; INS_InsertCall(ins,
								IPOINT_BEFORE,
							(AFUNPTR)r2r_binary_opw,
							IARG_FAST_ANALYSIS_CALL,
								IARG_REG_VALUE,
								thread_ctx_ptr,
								IARG_UINT32,
							REG16_INDX(reg_dst),
								IARG_UINT32,
							REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
								IARG_END);
					}
				}
				/* 8-bit operands */
				else {
					/* check for x86 clear register idiom */
					switch (ins_indx) {
						/* xor, sub, sbb */
						case XED_ICLASS_XOR:
						case XED_ICLASS_SUB:
						case XED_ICLASS_SBB:
							/* same dst, src */
							if (reg_dst == reg_src)
							{
							/* 8-bit upper */
						if (REG_is_Upper8(reg_dst)){
								/* clear */
							TRACEOUT; INS_InsertCall(ins,
								IPOINT_BEFORE,
							(AFUNPTR)r_clrb_u,
							IARG_FAST_ANALYSIS_CALL,
								IARG_REG_VALUE,
								thread_ctx_ptr,
								IARG_UINT32,
							REG8_INDX(reg_dst),
								IARG_END);}
							/* 8-bit lower */
						else{
								/* clear */
							TRACEOUT; INS_InsertCall(ins,
								IPOINT_BEFORE,
							(AFUNPTR)r_clrb_l,
							IARG_FAST_ANALYSIS_CALL,
								IARG_REG_VALUE,
								thread_ctx_ptr,
								IARG_UINT32,
							REG8_INDX(reg_dst),
								IARG_END);}

								/* done */
								break;
							}
						/* default behavior */
						default:
						/* propagate tags accordingly */
					if (REG_is_Lower8(reg_dst) &&
							REG_is_Lower8(reg_src)){
						/* lower 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_binary_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if(REG_is_Upper8(reg_dst) &&
							REG_is_Upper8(reg_src)){
						/* upper 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_binary_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if (REG_is_Lower8(reg_dst)){
						/*
						 * destination register is a
						 * lower 8-bit register and
						 * source register is an upper
						 * 8-bit register
						 */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_binary_opb_lu,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else
						/*
						 * destination register is an
						 * upper 8-bit register and
						 * source register is a lower
						 * 8-bit register
						 */
					{TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_binary_opb_ul,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					}
				}
			}
			/*
			 * 2nd operand is memory;
			 * we optimize for that case, since most
			 * instructions will have a register as
			 * the first operand -- leave the result
			 * into the reg and use it later
			 */
			else if (INS_OperandIsMemory(ins, OP_1)) {
				/* extract the register operand */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_binary_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_binary_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (upper) */
				else if (REG_is_Upper8(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_binary_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_binary_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* 1st operand is memory */
			else {
				/* extract the register operand */
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_binary_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_binary_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (upper) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_binary_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_binary_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/* bsf */
		case XED_ICLASS_BSF:
		/* bsr */
		case XED_ICLASS_BSR:
		/* mov */
		case XED_ICLASS_MOV:
			/*
			 * the general format of these instructions
			 * is the following: dst = src. We move the
			 * tag of the source to the destination
			 * (i.e., t[dst] = t[src])
			 */
			/*
			 * 2nd operand is immediate or segment register;
			 * clear the destination
			 *
			 * NOTE: When the processor moves a segment register
			 * into a 32-bit general-purpose register, it assumes
			 * that the 16 least-significant bits of the
			 * general-purpose register are the destination or
			 * source operand. If the register is a destination
			 * operand, the resulting value in the two high-order
			 * bytes of the register is implementation dependent.
			 * For the Pentium 4, Intel Xeon, and P6 family
			 * processors, the two high-order bytes are filled with
			 * zeros; for earlier 32-bit IA-32 processors, the two
			 * high order bytes are undefined.
			 */
			//KEYTRACE0;
			if (INS_OperandIsImmediate(ins, OP_1) ||
				(INS_OperandIsReg(ins, OP_1) &&
				REG_is_seg(INS_OperandReg(ins, OP_1)))) {
				/* destination operand is a memory address */
				if (INS_OperandIsMemory(ins, OP_0)) {
					/* clear n-bytes */
					switch (INS_OperandWidth(ins, OP_0)) {
						/* 4 bytes */
						case MEM_LONG_LEN:
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)tagmap_clrl,
							IARG_FAST_ANALYSIS_CALL,
							IARG_MEMORYWRITE_EA,
							IARG_END);

							/* done */
							break;
						/* 2 bytes */
						case MEM_WORD_LEN:
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)tagmap_clrw,
							IARG_FAST_ANALYSIS_CALL,
							IARG_MEMORYWRITE_EA,
							IARG_END);

							/* done */
							break;
						/* 1 byte */
						case MEM_BYTE_LEN:
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)tagmap_clrb,
							IARG_FAST_ANALYSIS_CALL,
							IARG_MEMORYWRITE_EA,
							IARG_END);

							/* done */
							break;
						/* make the compiler happy */
						default:
						LOG(string(__func__) +
						": unhandled operand width (" +
						INS_Disassemble(ins) + ")\n");

							/* done */
							return;
					}
				}
				/* destination operand is a register */
				else if (INS_OperandIsReg(ins, OP_0)) {
					/* extract the operand */
					reg_dst = INS_OperandReg(ins, OP_0);

					/* 32-bit operand */
					if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)r_clrl,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
							IARG_END);}
					/* 16-bit operand */
					else if (REG_is_gr16(reg_dst)){
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)r_clrw,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
							IARG_END);}
					/* 8-bit operand (upper) */
					else if (REG_is_Upper8(reg_dst)){
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)r_clrb_u,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
							IARG_END);}
					/* 8-bit operand (lower) */
					else{
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)r_clrb_l,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
							IARG_END);}
				}
			}
			/* both operands are registers */
			else if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)){
					/* propagate tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands */
				else if (REG_is_gr8(reg_dst)) {
					/* propagate tag accordingly */
					if (REG_is_Lower8(reg_dst) &&
							REG_is_Lower8(reg_src)){
						/* lower 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if(REG_is_Upper8(reg_dst) &&
							REG_is_Upper8(reg_src)){
						/* upper 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if (REG_is_Lower8(reg_dst)){
						/*
						 * destination register is a
						 * lower 8-bit register and
						 * source register is an upper
						 * 8-bit register
						 */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opb_lu,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else{
						/*
						 * destination register is an
						 * upper 8-bit register and
						 * source register is a lower
						 * 8-bit register
						 */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opb_ul,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				}
			}
			/*
			 * 2nd operand is memory;
			 * we optimize for that case, since most
			 * instructions will have a register as
			 * the first operand -- leave the result
			 * into the reg and use it later
			 */
			else if (INS_OperandIsMemory(ins, OP_1)) {
				/* extract the register operand */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
				    //KEYTRACE0;
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (upper) */
				else if (REG_is_Upper8(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG8_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG8_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* 1st operand is memory */
			else {
				/* extract the register operand */
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (upper) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_xfer_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_xfer_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/* conditional movs */
		case XED_ICLASS_CMOVB:
		case XED_ICLASS_CMOVBE:
		case XED_ICLASS_CMOVL:
		case XED_ICLASS_CMOVLE:
		case XED_ICLASS_CMOVNB:
		case XED_ICLASS_CMOVNBE:
		case XED_ICLASS_CMOVNL:
		case XED_ICLASS_CMOVNLE:
		case XED_ICLASS_CMOVNO:
		case XED_ICLASS_CMOVNP:
		case XED_ICLASS_CMOVNS:
		case XED_ICLASS_CMOVNZ:
		case XED_ICLASS_CMOVO:
		case XED_ICLASS_CMOVP:
		case XED_ICLASS_CMOVS:
		case XED_ICLASS_CMOVZ:
			/*
			 * the general format of these instructions
			 * is the following: dst = src iff cond. We
			 * move the tag of the source to the destination
			 * iff the corresponding condition is met
			 * (i.e., t[dst] = t[src])
			 */
			/* both operands are registers */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					INS_InsertPredicatedCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else{
					/* propagate tag accordingly */
					INS_InsertPredicatedCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/*
			 * 2nd operand is memory;
			 * we optimize for that case, since most
			 * instructions will have a register as
			 * the first operand -- leave the result
			 * into the reg and use it later
			 */
			else {
				/* extract the register operand */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					INS_InsertPredicatedCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else{
					/* propagate the tag accordingly */
					INS_InsertPredicatedCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/*
		 * cbw;
		 * move the tag associated with AL to AH
		 *
		 * NOTE: sign extension generates data that
		 * are dependent to the source operand
		 */
		case XED_ICLASS_CBW:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r2r_xfer_opb_ul,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG8_INDX(REG_AH),
				IARG_UINT32, REG8_INDX(REG_AL),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/*
		 * cwd;
		 * move the tag associated with AX to DX
		 *
		 * NOTE: sign extension generates data that
		 * are dependent to the source operand
		 */
		case XED_ICLASS_CWD:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r2r_xfer_opw,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG16_INDX(REG_DX),
				IARG_UINT32, REG16_INDX(REG_AX),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/*
		 * cwde;
		 * move the tag associated with AX to EAX
		 *
		 * NOTE: sign extension generates data that
		 * are dependent to the source operand
		 */
		case XED_ICLASS_CWDE:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)_cwde,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/*
		 * cdq;
		 * move the tag associated with EAX to EDX
		 *
		 * NOTE: sign extension generates data that
		 * are dependent to the source operand
		 */
		case XED_ICLASS_CDQ:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r2r_xfer_opl,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG32_INDX(REG_EDX),
				IARG_UINT32, REG32_INDX(REG_EAX),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/*
		 * movsx;
		 *
		 * NOTE: sign extension generates data that
		 * are dependent to the source operand
		 */
		case XED_ICLASS_MOVSX:
			/*
			 * the general format of these instructions
			 * is the following: dst = src. We move the
			 * tag of the source to the destination
			 * (i.e., t[dst] = t[src]) and we extend the
			 * tag bits accordingly
			 */
			/* both operands are registers */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 16-bit & 8-bit operands */
				if (REG_is_gr16(reg_dst)) {
					/* upper 8-bit */
					if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_r2r_opwb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_r2r_opwb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				}
				/* 32-bit & 16-bit operands */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_r2r_oplw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 8-bit operands (upper 8-bit) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_r2r_oplb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 8-bit operands (lower 8-bit) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_r2r_oplb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* 2nd operand is memory */
			else {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 16-bit & 8-bit operands */
				if (REG_is_gr16(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_m2r_opwb,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 16-bit operands */
				else if (INS_MemoryWriteSize(ins) ==
						BIT2BYTE(MEM_WORD_LEN)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_m2r_oplw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 8-bit operands */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movsx_m2r_oplb,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/*
		 * movzx;
		 *
		 * NOTE: zero extension always results in
		 * clearing the tags associated with the
		 * higher bytes of the destination operand
		 */
		case XED_ICLASS_MOVZX:
			/*
			 * the general format of these instructions
			 * is the following: dst = src. We move the
			 * tag of the source to the destination
			 * (i.e., t[dst] = t[src]) and we extend the
			 * tag bits accordingly
			 */
			/* both operands are registers */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 16-bit & 8-bit operands */
				if (REG_is_gr16(reg_dst)) {
					/* upper 8-bit */
					if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_r2r_opwb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_r2r_opwb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				}
				/* 32-bit & 16-bit operands */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_r2r_oplw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 8-bit operands (upper 8-bit) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_r2r_oplb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 8-bit operands (lower 8-bit) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_r2r_oplb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* 2nd operand is memory */
			else {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 16-bit & 8-bit operands */
				if (REG_is_gr16(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_m2r_opwb,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 16-bit operands */
				else if (INS_MemoryWriteSize(ins) ==
						BIT2BYTE(MEM_WORD_LEN)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_m2r_oplw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 32-bit & 8-bit operands */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_movzx_m2r_oplb,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/* div */
		case XED_ICLASS_DIV:
		/* idiv */
		case XED_ICLASS_IDIV:
		/* mul */
		case XED_ICLASS_MUL:
			/*
			 * the general format of these brain-dead and
			 * totally corrupted instructions is the following:
			 * dst1:dst2 {*, /}= src. We tag the destination
			 * operands if the source is also taged
			 * (i.e., t[dst1]:t[dst2] |= t[src])
			 */
			/* memory operand */
			if (INS_OperandIsMemory(ins, OP_0))
				/* differentiate based on the memory size */
				switch (INS_MemoryWriteSize(ins)) {
					/* 4 bytes */
					case BIT2BYTE(MEM_LONG_LEN):
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)m2r_ternary_opl,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);

						/* done */
						break;
					/* 2 bytes */
					case BIT2BYTE(MEM_WORD_LEN):
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)m2r_ternary_opw,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);

						/* done */
						break;
					/* 1 byte */
					case BIT2BYTE(MEM_BYTE_LEN):
					default:
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)m2r_ternary_opb,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);

						/* done */
						break;
				}
			/* register operand */
			else {
				/* extract the operand */
				reg_src = INS_OperandReg(ins, OP_0);

				/* 32-bit operand */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operand */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (upper) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/*
		 * imul;
		 * I'm still wondering how brain-damaged the
		 * ISA architect should be in order to come
		 * up with something so ugly as the IMUL
		 * instruction
		 */
		case XED_ICLASS_IMUL:
			/* one-operand form */
			if (INS_OperandIsImplicit(ins, OP_1)) {
				/* memory operand */
				if (INS_OperandIsMemory(ins, OP_0))
				/* differentiate based on the memory size */
				switch (INS_MemoryWriteSize(ins)) {
					/* 4 bytes */
					case BIT2BYTE(MEM_LONG_LEN):
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)m2r_ternary_opl,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);

						/* done */
						break;
					/* 2 bytes */
					case BIT2BYTE(MEM_WORD_LEN):
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)m2r_ternary_opw,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);

						/* done */
						break;
					/* 1 byte */
					case BIT2BYTE(MEM_BYTE_LEN):
					default:
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)m2r_ternary_opb,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);

						/* done */
						break;
				}
			/* register operand */
			else {
				/* extract the operand */
				reg_src = INS_OperandReg(ins, OP_0);

				/* 32-bit operand */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operand */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (upper) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_ternary_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				}
			}
			/* two/three-operands form */
			else {
				/* 2nd operand is immediate; do nothing */
				if (INS_OperandIsImmediate(ins, OP_1))
					break;

				/* both operands are registers */
				if (INS_MemoryOperandCount(ins) == 0) {
					/* extract the operands */
					reg_dst = INS_OperandReg(ins, OP_0);
					reg_src = INS_OperandReg(ins, OP_1);

					/* 32-bit operands */
					if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)r2r_binary_opl,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);}
					/* 16-bit operands */
					else{
					/* propagate tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)r2r_binary_opw,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);}
				}
				/*
				 * 2nd operand is memory;
				 * we optimize for that case, since most
				 * instructions will have a register as
				 * the first operand -- leave the result
				 * into the reg and use it later
				 */
				else {
					/* extract the register operand */
					reg_dst = INS_OperandReg(ins, OP_0);

					/* 32-bit operands */
					if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)m2r_binary_opl,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);}
					/* 16-bit operands */
					else{
					/* propagate the tag accordingly */
						TRACEOUT; INS_InsertCall(ins,
							IPOINT_BEFORE,
							(AFUNPTR)m2r_binary_opw,
							IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
							IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
							IARG_END);}
				}
			}

			/* done */
			break;
		/* conditional sets */
		case XED_ICLASS_SETB:
		case XED_ICLASS_SETBE:
		case XED_ICLASS_SETL:
		case XED_ICLASS_SETLE:
		case XED_ICLASS_SETNB:
		case XED_ICLASS_SETNBE:
		case XED_ICLASS_SETNL:
		case XED_ICLASS_SETNLE:
		case XED_ICLASS_SETNO:
		case XED_ICLASS_SETNP:
		case XED_ICLASS_SETNS:
		case XED_ICLASS_SETNZ:
		case XED_ICLASS_SETO:
		case XED_ICLASS_SETP:
		case XED_ICLASS_SETS:
		case XED_ICLASS_SETZ:
			/*
			 * clear the tag information associated with the
			 * destination operand
			 */
			/* register operand */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operand */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 8-bit operand (upper) */
				if (REG_is_Upper8(reg_dst)){
				    TRACEOUT;
					/* propagate tag accordingly */
					INS_InsertPredicatedCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)r_clrb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_END);
				}
				/* 8-bit operand (lower) */
				else{
				    TRACEOUT;
					/* propagate tag accordingly */
					INS_InsertPredicatedCall(ins,
							IPOINT_BEFORE,
						(AFUNPTR)r_clrb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_END);}
			}
			/* memory operand */
			else{
			    TRACEOUT;
				/* propagate the tag accordingly */
				INS_InsertPredicatedCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)tagmap_clrb,
					IARG_FAST_ANALYSIS_CALL,
					IARG_MEMORYWRITE_EA,
					IARG_END);}

			/* done */
			break;
		/*
		 * stmxcsr;
		 * clear the destination operand (register only)
		 */
		case XED_ICLASS_STMXCSR:
			/* propagate tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)tagmap_clrl,
				IARG_FAST_ANALYSIS_CALL,
				IARG_MEMORYWRITE_EA,
				IARG_END);

			/* done */
			break;
		/* smsw */
		case XED_ICLASS_SMSW:
		/* str */
		case XED_ICLASS_STR:
			/*
			 * clear the tag information associated with
			 * the destination operand
			 */
			/* register operand */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operand */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 16-bit register */
				if (REG_is_gr16(reg_dst)){
					/* propagate tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r_clrw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_END);}
				/* 32-bit register */
				else{
					/* propagate tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r_clrl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_END);}
			}
			/* memory operand */
			else{
				/* propagate tag accordingly */
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)tagmap_clrw,
					IARG_FAST_ANALYSIS_CALL,
					IARG_MEMORYWRITE_EA,
					IARG_END);}

			/* done */
			break;
		/*
		 * lar;
		 * clear the destination operand (register only)
		 */
		case XED_ICLASS_LAR:
			/* extract the 1st operand */
			reg_dst = INS_OperandReg(ins, OP_0);

			/* 16-bit register */
			if (REG_is_gr16(reg_dst)){
				/* propagate tag accordingly */
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r_clrw,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_END);}
			/* 32-bit register */
			else{
				/* propagate tag accordingly */
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r_clrl,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_END);}

			/* done */
			break;
		/* rdpmc */
		case XED_ICLASS_RDPMC:
		/* rdtsc */
		case XED_ICLASS_RDTSC:{
			/*
			 * clear the tag information associated with
			 * EAX and EDX
			 */
			/* propagate tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r_clrl2,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_END);}

			/* done */
			break;
		/*
		 * cpuid;
		 * clear the tag information associated with
		 * EAX, EBX, ECX, and EDX
		 */
		case XED_ICLASS_CPUID:
			/* propagate tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r_clrl4,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_END);

			/* done */
			break;
		/*
		 * lahf;
		 * clear the tag information of AH
		 */
		case XED_ICLASS_LAHF:
			/* propagate tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r_clrb_u,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG8_INDX(REG_AH),
				IARG_END);

			/* done */
			break;
		/*
		 * cmpxchg;
		 * t[dst] = t[src] iff EAX/AX/AL == dst, else
		 * t[EAX/AX/AL] = t[dst] -- yes late-night coding again
		 * and I'm really tired to comment this crap...
		 */
		case XED_ICLASS_CMPXCHG:
			/* both operands are registers */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)) {
				/* propagate tag accordingly; fast path */
					INS_InsertIfCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_r2r_opl_fast,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_REG_VALUE, REG_EAX,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_REG_VALUE, reg_dst,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				/* propagate tag accordingly; slow path */
					INS_InsertThenCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_r2r_opl_slow,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)) {
				/* propagate tag accordingly; fast path */
					INS_InsertIfCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_r2r_opw_fast,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_REG_VALUE, REG_AX,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_REG_VALUE, reg_dst,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				/* propagate tag accordingly; slow path */
					INS_InsertThenCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_r2r_opw_slow,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				}
				/* 8-bit operands */
				else
				LOG(string(__func__) +
					": unhandled opcode (opcode=" +
					decstr(ins_indx) + ")\n");
			}
			/* 1st operand is memory */
			else {
				/* extract the operand */
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_src)) {
				/* propagate tag accordingly; fast path */
					INS_InsertIfCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_m2r_opl_fast,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_REG_VALUE, REG_EAX,
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				/* propagate tag accordingly; slow path */
					INS_InsertThenCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_r2m_opl_slow,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_src)) {
				/* propagate tag accordingly; fast path */
					INS_InsertIfCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_m2r_opw_fast,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_REG_VALUE, REG_AX,
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				/* propagate tag accordingly; slow path */
					INS_InsertThenCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_cmpxchg_r2m_opw_slow,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				}
				/* 8-bit operands */
				else
				LOG(string(__func__) +
					": unhandled opcode (opcode=" +
					decstr(ins_indx) + ")\n");
			}

			/* done */
			break;
		/*
		 * xchg;
		 * exchange the tag information of the two operands
		 */
		case XED_ICLASS_XCHG:
			/* both operands are registers */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)) {
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, 8,
					IARG_UINT32, REG32_INDX(reg_dst),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
					IARG_UINT32, 8,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)){
					/* propagate tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_r2r_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands */
				else if (REG_is_gr8(reg_dst)) {
					/* propagate tag accordingly */
					if (REG_is_Lower8(reg_dst) &&
						REG_is_Lower8(reg_src)){
						/* lower 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_r2r_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if(REG_is_Upper8(reg_dst) &&
						REG_is_Upper8(reg_src)){
						/* upper 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_r2r_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if (REG_is_Lower8(reg_dst)){
						/*
						 * destination register is a
						 * lower 8-bit register and
						 * source register is an upper
						 * 8-bit register
						 */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_r2r_opb_lu,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else{
						/*
						 * destination register is an
						 * upper 8-bit register and
						 * source register is a lower
						 * 8-bit register
						 */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_r2r_opb_ul,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				}
			}
			/*
			 * 2nd operand is memory;
			 * we optimize for that case, since most
			 * instructions will have a register as
			 * the first operand -- leave the result
			 * into the reg and use it later
			 */
			else if (INS_OperandIsMemory(ins, OP_1)) {
				/* extract the register operand */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (upper) */
				else if (REG_is_Upper8(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* 1st operand is memory */
			else {
				/* extract the register operand */
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (upper) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG8_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xchg_m2r_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG8_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/*
		 * xadd;
		 * xchg + add. We instrument this instruction  using the tag
		 * logic of xchg and add (see above)
		 */
		case XED_ICLASS_XADD:
			/* both operands are registers */
			if (INS_MemoryOperandCount(ins) == 0) {
				/* extract the operands */
				reg_dst = INS_OperandReg(ins, OP_0);
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)) {
					/* propagate the tag accordingly */
					TRACEOUT;
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, 8,
					IARG_UINT32, REG32_INDX(reg_dst),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
						IARG_UINT32, 8,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_binary_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_dst)){
					/* propagate tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_r2r_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operands */
				else if (REG_is_gr8(reg_dst)) {
					/* propagate tag accordingly */
					if (REG_is_Lower8(reg_dst) &&
						REG_is_Lower8(reg_src)){
						/* lower 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_r2r_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if(REG_is_Upper8(reg_dst) &&
						REG_is_Upper8(reg_src)){
						/* upper 8-bit registers */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_r2r_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else if (REG_is_Lower8(reg_dst)){
						/*
						 * destination register is a
						 * lower 8-bit register and
						 * source register is an upper
						 * 8-bit register
						 */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_r2r_opb_lu,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
					else{
						/*
						 * destination register is an
						 * upper 8-bit register and
						 * source register is a lower
						 * 8-bit register
						 */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_r2r_opb_ul,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_dst),
						IARG_UINT32, REG8_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				}
			}
			/* 1st operand is memory */
			else {
				/* extract the register operand */
				reg_src = INS_OperandReg(ins, OP_1);

				/* 32-bit operands */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_m2r_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else if (REG_is_gr16(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_m2r_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (upper) */
				else if (REG_is_Upper8(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_m2r_opb_u,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 8-bit operand (lower) */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_xadd_m2r_opb_l,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32, REG8_INDX(reg_src),
						IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/* xlat; similar to a mov between a memory location and AL */
		case XED_ICLASS_XLAT:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2r_xfer_opb_l,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG8_INDX(REG_AL),
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* lodsb; similar to a mov between a memory location and AL */
		case XED_ICLASS_LODSB:
			/* propagate the tag accordingly */
			INS_InsertPredicatedCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2r_xfer_opb_l,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG8_INDX(REG_AL),
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* lodsw; similar to a mov between a memory location and AX */
		case XED_ICLASS_LODSW:
			/* propagate the tag accordingly */
			INS_InsertPredicatedCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2r_xfer_opw,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG16_INDX(REG_AX),
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* lodsd; similar to a mov between a memory location and EAX */
		case XED_ICLASS_LODSD:
			/* propagate the tag accordingly */
			INS_InsertPredicatedCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2r_xfer_opl,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG32_INDX(REG_EAX),
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/*
		 * stosb;
		 * the opposite of lodsb; however, since the instruction can
		 * also be prefixed with 'rep', the analysis code moves the
		 * tag information, accordingly, only once (i.e., before the
		 * first repetition) -- typically this will not lead in
		 * inlined code
		 */
		case XED_ICLASS_STOSB:
			/* the instruction is rep prefixed */
			if (INS_RepPrefix(ins)) {
				/* propagate the tag accordingly */
				INS_InsertIfPredicatedCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)rep_predicate,
					IARG_FAST_ANALYSIS_CALL,
					IARG_FIRST_REP_ITERATION,
					IARG_END);
				INS_InsertThenPredicatedCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2m_xfer_opbn,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_MEMORYWRITE_EA,
				IARG_REG_VALUE, INS_RepCountRegister(ins),
				IARG_REG_VALUE, INS_OperandReg(ins, OP_4),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);
			}
			/* no rep prefix */
			else
				/* the instruction is not rep prefixed */
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2m_xfer_opb_l,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG8_INDX(REG_AL),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);

			/* done */
			break;
		/*
		 * stosw;
		 * the opposite of lodsw; however, since the instruction can
		 * also be prefixed with 'rep', the analysis code moves the
		 * tag information, accordingly, only once (i.e., before the
		 * first repetition) -- typically this will not lead in
		 * inlined code
		 */
		case XED_ICLASS_STOSW:
			/* the instruction is rep prefixed */
			if (INS_RepPrefix(ins)) {
				/* propagate the tag accordingly */
				INS_InsertIfPredicatedCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)rep_predicate,
					IARG_FAST_ANALYSIS_CALL,
					IARG_FIRST_REP_ITERATION,
					IARG_END);
				INS_InsertThenPredicatedCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2m_xfer_opwn,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_MEMORYWRITE_EA,
				IARG_REG_VALUE, INS_RepCountRegister(ins),
				IARG_REG_VALUE, INS_OperandReg(ins, OP_4),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);
			}
			/* no rep prefix */
			else
				/* the instruction is not rep prefixed */
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2m_xfer_opw,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG16_INDX(REG_AX),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);

			/* done */
			break;
		/*
		 * stosd;
		 * the opposite of lodsd; however, since the instruction can
		 * also be prefixed with 'rep', the analysis code moves the
		 * tag information, accordingly, only once (i.e., before the
		 * first repetition) -- typically this will not lead in
		 * inlined code
		 */
		case XED_ICLASS_STOSD:
			/* the instruction is rep prefixed */
			if (INS_RepPrefix(ins)) {
				/* propagate the tag accordingly */
				INS_InsertIfPredicatedCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)rep_predicate,
					IARG_FAST_ANALYSIS_CALL,
					IARG_FIRST_REP_ITERATION,
					IARG_END);
				INS_InsertThenPredicatedCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2m_xfer_opln,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_MEMORYWRITE_EA,
				IARG_REG_VALUE, INS_RepCountRegister(ins),
				IARG_REG_VALUE, INS_OperandReg(ins, OP_4),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);
			}
			/* no rep prefix */
			else
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2m_xfer_opl,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG32_INDX(REG_EAX),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);

			/* done */
			break;
		/* movsd */
		case XED_ICLASS_MOVSD:
            //KEYTRACE0;
			/* propagate the tag accordingly */
			INS_InsertPredicatedCall(ins,
			//INS_InsertPredicatedCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2m_xfer_opl,
				IARG_FAST_ANALYSIS_CALL,
				IARG_MEMORYWRITE_EA,
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* movsw */
		case XED_ICLASS_MOVSW:
			/* propagate the tag accordingly */
			INS_InsertPredicatedCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2m_xfer_opw,
				IARG_FAST_ANALYSIS_CALL,
				IARG_MEMORYWRITE_EA,
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* movsb */
		case XED_ICLASS_MOVSB:
			/* propagate the tag accordingly */
			INS_InsertPredicatedCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2m_xfer_opb,
				IARG_FAST_ANALYSIS_CALL,
				IARG_MEMORYWRITE_EA,
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* sal */
		case XED_ICLASS_SALC:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r_clrb_l,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_UINT32, REG8_INDX(REG_AL),
				IARG_END);

			/* done */
			break;
		/* TODO: shifts are not handled (yet) */
		/* rcl */
		case XED_ICLASS_RCL:
		/* rcr */
		case XED_ICLASS_RCR:
		/* rol */
		case XED_ICLASS_ROL:
		/* ror */
		case XED_ICLASS_ROR:
		/* sal/shl */
		case XED_ICLASS_SHL:
		/* sar */
		case XED_ICLASS_SAR:
		/* shr */
		case XED_ICLASS_SHR:
		/* shld */
		case XED_ICLASS_SHLD:
		case XED_ICLASS_SHRD:

			/* done */
			break;
		/* pop; mov equivalent (see above) */
		case XED_ICLASS_POP:
			/* register operand */
			if (INS_OperandIsReg(ins, OP_0)) {
				/* extract the operand */
				reg_dst = INS_OperandReg(ins, OP_0);

				/* 32-bit operand */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2r_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* memory operand */
			else if (INS_OperandIsMemory(ins, OP_0)) {
				/* 32-bit operand */
				if (INS_MemoryWriteSize(ins) ==
						BIT2BYTE(MEM_LONG_LEN)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2m_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2m_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/* push; mov equivalent (see above) */
		case XED_ICLASS_PUSH:
			/* register operand */
			if (INS_OperandIsReg(ins, OP_0)) {
				/* extract the operand */
				reg_src = INS_OperandReg(ins, OP_0);

				/* 32-bit operand */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2m_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_MEMORYWRITE_EA,
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* memory operand */
			else if (INS_OperandIsMemory(ins, OP_0)) {
				/* 32-bit operand */
				if (INS_MemoryWriteSize(ins) ==
						BIT2BYTE(MEM_LONG_LEN)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2m_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)m2m_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* immediate or segment operand; clean */
			else {
				/* clear n-bytes */
				switch (INS_OperandWidth(ins, OP_0)) {
					/* 4 bytes */
					case MEM_LONG_LEN:
				/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);

						/* done */
						break;
					/* 2 bytes */
					case MEM_WORD_LEN:
				/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);

						/* done */
						break;
					/* 1 byte */
					case MEM_BYTE_LEN:
				/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrb,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);

						/* done */
						break;
					/* make the compiler happy */
					default:
						/* done */
						break;
				}
			}

			/* done */
			break;
		/* popa;
		 * similar to pop but for all the 16-bit
		 * general purpose registers
		 */
		case XED_ICLASS_POPA:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2r_restore_opw,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* popad;
		 * similar to pop but for all the 32-bit
		 * general purpose registers
		 */
		case XED_ICLASS_POPAD:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)m2r_restore_opl,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* pusha;
		 * similar to push but for all the 16-bit
		 * general purpose registers
		 */
		case XED_ICLASS_PUSHA:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r2m_save_opw,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* pushad;
		 * similar to push but for all the 32-bit
		 * general purpose registers
		 */
		case XED_ICLASS_PUSHAD:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)r2m_save_opl,
				IARG_FAST_ANALYSIS_CALL,
				IARG_REG_VALUE, thread_ctx_ptr,
				IARG_MEMORYWRITE_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
				IARG_END);

			/* done */
			break;
		/* pushf; clear a memory word (i.e., 16-bits) */
		case XED_ICLASS_PUSHF:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)tagmap_clrw,
				IARG_FAST_ANALYSIS_CALL,
				IARG_MEMORYWRITE_EA,
				IARG_END);

			/* done */
			break;
		/* pushfd; clear a double memory word (i.e., 32-bits) */
		case XED_ICLASS_PUSHFD:
			/* propagate the tag accordingly */
			TRACEOUT; INS_InsertCall(ins,
				IPOINT_BEFORE,
				(AFUNPTR)tagmap_clrl,
				IARG_FAST_ANALYSIS_CALL,
				IARG_MEMORYWRITE_EA,
				IARG_END);

			/* done */
			break;
		/* call (near); similar to push (see above) */
		case XED_ICLASS_CALL_NEAR:
			/* relative target */
                    INS_InsertCall(ins,
						IPOINT_BEFORE ,
						(AFUNPTR)trace_call,
						IARG_FAST_ANALYSIS_CALL,
						IARG_CONTEXT ,
						IARG_THREAD_ID,
						IARG_INST_PTR,
						IARG_END);
			if (INS_OperandIsImmediate(ins, OP_0)) {
				/* 32-bit operand */
				if (INS_OperandWidth(ins, OP_0) == MEM_LONG_LEN){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);}
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);}
			}
			/* absolute target; register */
			else if (INS_OperandIsReg(ins, OP_0)) {
				/* extract the source register */
				reg_src = INS_OperandReg(ins, OP_0);

				/* 32-bit operand */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);}
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);}
			}
			/* absolute target; memory */
			else {
				/* 32-bit operand */
				if (INS_OperandWidth(ins, OP_0) == MEM_LONG_LEN){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);}
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)tagmap_clrw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_MEMORYWRITE_EA,
						IARG_END);}
			}

			/* done */
			break;
		/*
		 * leave;
		 * similar to a mov between ESP/SP and EBP/BP, and a pop
		 */
		case XED_ICLASS_LEAVE:
			/* extract the operands */
			reg_dst = INS_OperandReg(ins, OP_3);
			reg_src = INS_OperandReg(ins, OP_2);

			/* 32-bit operands */
			if (REG_is_gr32(reg_dst)) {
				/* propagate the tag accordingly */
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2r_xfer_opl,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);
                    INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)m2r_xfer_opl,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
					IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);
			}
			/* 16-bit operands */
			else {
				/* propagate the tag accordingly */
				TRACEOUT; INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)r2r_xfer_opw,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);
                    INS_InsertCall(ins,
					IPOINT_BEFORE,
					(AFUNPTR)m2r_xfer_opw,
					IARG_FAST_ANALYSIS_CALL,
					IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_src),
					IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
					IARG_END);
			}

			/* done */
			break;
		/* lea */
		case XED_ICLASS_LEA:
			/*
			 * the general format of this instruction
			 * is the following: dst = src_base | src_indx.
			 * We move the tags of the source base and index
			 * registers to the destination
			 * (i.e., t[dst] = t[src_base] | t[src_indx])
			 */

			/* extract the operands */
			reg_base	= INS_MemoryBaseReg(ins);
			reg_indx	= INS_MemoryIndexReg(ins);
			reg_dst		= INS_OperandReg(ins, OP_0);

			/* no base or index register; clear the destination */
			if (reg_base == REG_INVALID() &&
					reg_indx == REG_INVALID()) {
				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* clear */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r_clrl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32,
						REG32_INDX(reg_dst),
						IARG_END);}
				/* 16-bit operands */
				else{
					/* clear */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r_clrw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
						IARG_UINT32,
						REG16_INDX(reg_dst),
						IARG_END);}
			}
			/* base register exists; no index register */
			if (reg_base != REG_INVALID() &&
					reg_indx == REG_INVALID()) {
				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_base),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else{
					/* propagate tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_base),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* index register exists; no base register */
			if (reg_base == REG_INVALID() &&
					reg_indx != REG_INVALID()) {
				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_indx),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else{
					/* propagate tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)r2r_xfer_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_indx),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}
			/* base and index registers exist */
			if (reg_base != REG_INVALID() &&
					reg_indx != REG_INVALID()) {
				/* 32-bit operands */
				if (REG_is_gr32(reg_dst)){
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_lea_r2r_opl,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_dst),
					IARG_UINT32, REG32_INDX(reg_base),
					IARG_UINT32, REG32_INDX(reg_indx),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
				/* 16-bit operands */
				else{
					/* propagate the tag accordingly */
					TRACEOUT; INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)_lea_r2r_opw,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG16_INDX(reg_dst),
					IARG_UINT32, REG16_INDX(reg_base),
					IARG_UINT32, REG16_INDX(reg_indx),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);}
			}

			/* done */
			break;
		/* cmpxchg */
		case XED_ICLASS_CMPXCHG8B:
		/* enter */
		case XED_ICLASS_ENTER:
			LOG(string(__func__) +
				": unhandled opcode (opcode=" +
				decstr(ins_indx) + ")\n");

			/* done */
			break;
        case XED_ICLASS_JB:
        case XED_ICLASS_JBE:
        case XED_ICLASS_JL:
        case XED_ICLASS_JLE:
        case XED_ICLASS_JMP:
        case XED_ICLASS_JNB:
        case XED_ICLASS_JNBE:
        case XED_ICLASS_JNL:
        case XED_ICLASS_JNLE:
        case XED_ICLASS_JNO:
        case XED_ICLASS_JNP:
        case XED_ICLASS_JNS:
        case XED_ICLASS_JNZ:
        case XED_ICLASS_JO:
        case XED_ICLASS_JP:
        case XED_ICLASS_JRCXZ:
        case XED_ICLASS_JS:
        case XED_ICLASS_JZ:


			if (INS_OperandIsImmediate(ins, OP_0)) {
                fprintf(stdout, "Jimmediate %s\n", INS_Disassemble(ins).c_str());
			}
			/* absolute target; register */
			else if (INS_OperandIsReg(ins, OP_0)) {
				/* extract the source register */
				reg_src = INS_OperandReg(ins, OP_0);

				/* 32-bit operand */
				if (REG_is_gr32(reg_src)){
					/* propagate the tag accordingly */
					TRACEOUT;
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)check_register_32bit,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
                }
				/* 16-bit operand */
				else{
					/* propagate the tag accordingly */
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)check_register_16bit,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
					IARG_UINT32, REG32_INDX(reg_src),
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
                }
			}
			/* absolute target; memory */

			else if (INS_OperandIsMemory(ins, OP_0)){
				/* 32-bit operand */
				if (INS_OperandWidth(ins, OP_0) == MEM_LONG_LEN){
					/* propagate the tag accordingly */
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)check_memory_32bit,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
                        IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				/* 16-bit operand */
				}
				else{
					/* propagate the tag accordingly */
					INS_InsertCall(ins,
						IPOINT_BEFORE,
						(AFUNPTR)check_memory_16bit,
						IARG_FAST_ANALYSIS_CALL,
						IARG_REG_VALUE, thread_ctx_ptr,
                        IARG_MEMORYREAD_EA,
                            IARG_THREAD_ID,
                            IARG_INST_PTR,
						IARG_END);
				}
			}
            else{
                //(void)fprintf(stdout, "other! %s\n", INS_Disassemble(ins).c_str());
            }
			/* done */
			break;
            case XED_ICLASS_RET_NEAR:
                    INS_InsertCall(ins,
						IPOINT_BEFORE ,
						(AFUNPTR)trace_ret,
						IARG_FAST_ANALYSIS_CALL,
						IARG_CONTEXT ,
						IARG_THREAD_ID,
						IARG_INST_PTR,
						IARG_END);
					break;
		/*
		 * default handler
		 */


		default:
//			 (void)fprintf(stdout, "%s\n",
//				INS_Disassemble(ins).c_str());
			break;
	}
//    getins();
//    fprintf(stdout, "final : %08X\n", getins());
//	clrins();
}
