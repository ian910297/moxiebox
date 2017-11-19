/* Simulator for the moxie processor
   Copyright 2014 Anthony Green
   Distributed under the MIT/X11 software license, see the accompanying
   file COPYING or http://www.opensource.org/licenses/mit-license.php.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <endian.h>
#include "sandbox.h"

#define INLINE inline

static FILE *tracefile = stderr;

/* Extract the signed 10-bit offset from a 16-bit branch
   instruction.  */
#define INST2OFFSET(o) \
    ((((signed short) ((o & ((1 << 10) - 1)) << 6)) >> 6) << 1)

#define EXTRACT_WORD(addr) extract_word32(mach, addr)
#define EXTRACT_WORD16(addr) extract_word16(mach, addr)
#define EXTRACT_OFFSET(addr) (((int32_t) EXTRACT_WORD16(addr) << 16) >> 16)

static inline uint16_t extract_word16(machine &mach, uint32_t addr)
{
    uint32_t ret;
    if (!mach.read16(addr, ret))
        mach.cpu.asregs.exception = SIGBUS;

    uint16_t ret16 = (uint16_t) ret;
    return le16toh(ret16);
}

static inline uint32_t extract_word32(machine &mach, uint32_t addr)
{
    uint32_t ret;
    if (!mach.read32(addr, ret))
        mach.cpu.asregs.exception = SIGBUS;
    return le32toh(ret);
}

/* Write a 1 byte value to memory.  */

static void INLINE wbat(machine &mach, word addr, word v)
{
    if (!mach.write8(addr, v))
        mach.cpu.asregs.exception = SIGBUS;
}

/* Write a 2 byte value to memory.  */

static void INLINE wsat(machine &mach, word addr, word v)
{
    if (!mach.write16(addr, v))
        mach.cpu.asregs.exception = SIGBUS;
}

/* Write a 4 byte value to memory.  */

static void INLINE wlat(machine &mach, word addr, word v)
{
    if (!mach.write32(addr, v))
        mach.cpu.asregs.exception = SIGBUS;
}

/* Read 2 bytes from memory.  */

static int INLINE rsat(machine &mach, word addr)
{
    uint32_t ret;
    if (!mach.read16(addr, ret))
        mach.cpu.asregs.exception = SIGBUS;
    return (int32_t) ret;
}

/* Read 1 byte from memory.  */

static int INLINE rbat(machine &mach, word addr)
{
    uint32_t ret;
    if (!mach.read8(addr, ret))
        mach.cpu.asregs.exception = SIGBUS;
    return (int32_t) ret;
}

/* Read 4 bytes from memory.  */

static int INLINE rlat(machine &mach, word addr)
{
    uint32_t ret;
    if (!mach.read32(addr, ret))
        mach.cpu.asregs.exception = SIGBUS;
    return (int32_t) ret;
}

#if 0
#define TRACE(str)                                                             \
    if (mach.tracing)                                                          \
        fprintf(tracefile,                                                     \
                "0x%08x, %s, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, " \
                "0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",            \
                opc, str, cpu.asregs.regs[0], cpu.asregs.regs[1],              \
                cpu.asregs.regs[2], cpu.asregs.regs[3], cpu.asregs.regs[4],    \
                cpu.asregs.regs[5], cpu.asregs.regs[6], cpu.asregs.regs[7],    \
                cpu.asregs.regs[8], cpu.asregs.regs[9], cpu.asregs.regs[10],   \
                cpu.asregs.regs[11], cpu.asregs.regs[12], cpu.asregs.regs[13], \
                cpu.asregs.regs[14], cpu.asregs.regs[15]);
#else
#define TRACE(str)
#endif

static void sim_mmap(machine &mach)
{
    cpuState &cpu = mach.cpu;

    uint32_t addr = cpu.asregs.regs[2];
    int32_t length = cpu.asregs.regs[3];
    int32_t prot = cpu.asregs.regs[4];
    int32_t flags = cpu.asregs.regs[5];
    int32_t fd = cpu.asregs.regs[6];
    int32_t offset = cpu.asregs.regs[7];

    // ignore fd, offset
    (void) fd;
    (void) offset;

    if ((addr != 0) || (length < MACH_PAGE_SIZE) || (length & MACH_PAGE_MASK) ||
        ((uint32_t) length > mach.heapAvail) || (!(prot & MOXIE_PROT_READ)) ||
        (!(prot & MOXIE_PROT_WRITE)) || (!(prot & MOXIE_PROT_EXEC)) ||
        (!(flags & MOXIE_MAP_PRIVATE)) || (!(flags & MOXIE_MAP_ANONYMOUS))) {
        cpu.asregs.regs[2] = -EINVAL;
        return;
    }

    static unsigned int heapCount = 0;
    char tmpstr[32];

    sprintf(tmpstr, "heap%u", heapCount++);
    addressRange *rdr = new addressRange(tmpstr, length);
    rdr->buf.resize(length);
    rdr->updateRoot();
    rdr->readOnly = false;

    if (!mach.mapInsert(rdr)) {
        delete rdr;
        cpu.asregs.regs[2] = -ENOMEM;
    } else {
        cpu.asregs.regs[2] = rdr->start;
        mach.heapAvail -= length;
    }
}

int sim_resume(machine &mach, unsigned long long cpu_budget)
{
    int step = 0;

    word pc, opc;
    unsigned long long insts;
    unsigned short inst;
    cpuState &cpu = mach.cpu;

    cpu.asregs.exception = step ? SIGTRAP : 0;
    pc = cpu.asregs.regs[PC_REGNO];
    insts = cpu.asregs.insts;

    /* Run instructions here. */
    do {
        opc = pc;

        /* Fetch the instruction at pc.  */
        inst = EXTRACT_WORD16(pc);

        /* Decode instruction.  */
        if (inst & (1 << 15)) {
            if (inst & (1 << 14)) {
                /* This is a Form 3 instruction.  */
                int opcode = (inst >> 10 & 0xf);
                word flags[10] = {CC_EQ,         ~CC_EQ,        CC_LT,
                                  CC_GT,         CC_LTU,        CC_GTU,
                                  CC_GT | CC_EQ, CC_LT | CC_EQ, CC_GTU | CC_EQ,
                                  CC_LTU | CC_EQ};
                if (opcode < 10) {
                    if (cpu.asregs.cc & flags[opcode]) {
                        TRACE("BRANCH");
                        pc += INST2OFFSET(inst);
                        /* Increment basic block count */
                        if (mach.profiling)
                            mach.gprof_bb_data[pc + 2]++;
                    }
                } else {
                    TRACE("SIGILL3");
                    cpu.asregs.exception = SIGILL;
                    break;
                }
            } else {
                /* This is a Form 2 instruction.  */
                int opcode = (inst >> 12 & 0x3);
                switch (opcode) {
                case 0x00: /* inc */
                {
                    int a = (inst >> 8) & 0xf;
                    unsigned av = cpu.asregs.regs[a];
                    unsigned v = (inst & 0xff);

                    TRACE("inc");
                    cpu.asregs.regs[a] = av + v;
                } break;
                case 0x01: /* dec */
                {
                    int a = (inst >> 8) & 0xf;
                    unsigned av = cpu.asregs.regs[a];
                    unsigned v = (inst & 0xff);

                    TRACE("dec");
                    cpu.asregs.regs[a] = av - v;
                } break;
                case 0x02: /* gsr */
                {
                    int a = (inst >> 8) & 0xf;
                    unsigned v = (inst & 0xff);

                    TRACE("gsr");
                    cpu.asregs.regs[a] = cpu.asregs.sregs[v];
                } break;
                case 0x03: /* ssr */
                {
                    int a = (inst >> 8) & 0xf;
                    unsigned sreg = (inst & 0xff);
                    int32_t sval = cpu.asregs.regs[a];

                    TRACE("ssr");
                    switch (sreg) {
                    case 6: /* sim return buf addr */
                        if (!mach.physaddr(sval, 1))
                            cpu.asregs.exception = SIGBUS;
                        else
                            cpu.asregs.sregs[sreg] = sval;
                        break;
                    case 7: /* sim return buf length */
                        if (!cpu.asregs.sregs[6] ||
                            !mach.physaddr(cpu.asregs.sregs[6], sval))
                            cpu.asregs.exception = SIGBUS;
                        else
                            cpu.asregs.sregs[sreg] = sval;
                        break;
                    default:
                        cpu.asregs.sregs[sreg] = sval;
                        break;
                    }
                } break;
                default:
                    TRACE("SIGILL2");
                    cpu.asregs.exception = SIGILL;
                    break;
                }
            }
        } else {
            /* This is a Form 1 instruction.  */
            int opcode = inst >> 8;
            switch (opcode) {
            case 0x00: /* bad */
                opc = opcode;

                TRACE("SIGILL0");
                cpu.asregs.exception = SIGILL;
                break;
            case 0x01: /* ldi.l (immediate) */
            {
                int reg = (inst >> 4) & 0xf;

                TRACE("ldi.l");
                unsigned int val = EXTRACT_WORD(pc + 2);
                cpu.asregs.regs[reg] = val;
                pc += 4;
            } break;
            case 0x02: /* mov (register-to-register) */
            {
                int dest = (inst >> 4) & 0xf;
                int src = (inst) &0xf;

                TRACE("mov");
                cpu.asregs.regs[dest] = cpu.asregs.regs[src];
            } break;
            case 0x03: /* jsra */
            {
                unsigned int fn = EXTRACT_WORD(pc + 2);
                unsigned int sp = cpu.asregs.regs[1];

                TRACE("jsra");
                /* Save a slot for the static chain.  */
                sp -= 4;

                /* Push the return address.  */
                sp -= 4;
                wlat(mach, sp, pc + 6);

                /* Push the current frame pointer.  */
                sp -= 4;
                wlat(mach, sp, cpu.asregs.regs[0]);

                /* Uncache the stack pointer and set the pc and $fp.  */
                cpu.asregs.regs[1] = sp;
                cpu.asregs.regs[0] = sp;
                pc = fn - 2;
            } break;
            case 0x04: /* ret */
            {
                unsigned int sp = cpu.asregs.regs[0];

                TRACE("ret");

                /* Pop the frame pointer.  */
                cpu.asregs.regs[0] = rlat(mach, sp);
                sp += 4;

                /* Pop the return address.  */
                pc = rlat(mach, sp) - 2;
                sp += 4;

                /* Skip over the static chain slot.  */
                sp += 4;

                /* Uncache the stack pointer.  */
                cpu.asregs.regs[1] = sp;
            } break;
            case 0x05: /* add */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                unsigned av = cpu.asregs.regs[a];
                unsigned bv = cpu.asregs.regs[b];

                TRACE("add");
                cpu.asregs.regs[a] = av + bv;
            } break;
            case 0x06: /* push */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int sp = cpu.asregs.regs[a] - 4;

                TRACE("push");
                wlat(mach, sp, cpu.asregs.regs[b]);
                cpu.asregs.regs[a] = sp;
            } break;
            case 0x07: /* pop */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int sp = cpu.asregs.regs[a];

                TRACE("pop");
                cpu.asregs.regs[b] = rlat(mach, sp);
                cpu.asregs.regs[a] = sp + 4;
            } break;
            case 0x08: /* lda.l */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int addr = EXTRACT_WORD(pc + 2);

                TRACE("lda.l");
                cpu.asregs.regs[reg] = rlat(mach, addr);
                pc += 4;
            } break;
            case 0x09: /* sta.l */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int addr = EXTRACT_WORD(pc + 2);

                TRACE("sta.l");
                wlat(mach, addr, cpu.asregs.regs[reg]);
                pc += 4;
            } break;
            case 0x0a: /* ld.l (register indirect) */
            {
                int src = inst & 0xf;
                int dest = (inst >> 4) & 0xf;
                int xv;

                TRACE("ld.l");
                xv = cpu.asregs.regs[src];
                cpu.asregs.regs[dest] = rlat(mach, xv);
            } break;
            case 0x0b: /* st.l */
            {
                int dest = (inst >> 4) & 0xf;
                int val = inst & 0xf;

                TRACE("st.l");
                wlat(mach, cpu.asregs.regs[dest], cpu.asregs.regs[val]);
            } break;
            case 0x0c: /* ldo.l */
            {
                unsigned int addr = EXTRACT_OFFSET(pc + 2);
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;

                TRACE("ldo.l");
                addr += cpu.asregs.regs[b];
                cpu.asregs.regs[a] = rlat(mach, addr);
                pc += 2;
            } break;
            case 0x0d: /* sto.l */
            {
                unsigned int addr = EXTRACT_OFFSET(pc + 2);
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;

                TRACE("sto.l");
                addr += cpu.asregs.regs[a];
                wlat(mach, addr, cpu.asregs.regs[b]);
                pc += 2;
            } break;
            case 0x0e: /* cmp */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int cc = 0;
                int va = cpu.asregs.regs[a];
                int vb = cpu.asregs.regs[b];

                TRACE("cmp");

                if (va == vb)
                    cc = CC_EQ;
                else {
                    cc |= (va < vb ? CC_LT : 0);
                    cc |= (va > vb ? CC_GT : 0);
                    cc |= ((unsigned int) va < (unsigned int) vb ? CC_LTU : 0);
                    cc |= ((unsigned int) va > (unsigned int) vb ? CC_GTU : 0);
                }

                cpu.asregs.cc = cc;
            } break;
            case 0x0f: /* nop */
                break;
            case 0x10: /* sex.b */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                signed char bv = cpu.asregs.regs[b];

                TRACE("sex.b");
                cpu.asregs.regs[a] = (int) bv;
            } break;
            case 0x11: /* sex.s */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                signed short bv = cpu.asregs.regs[b];

                TRACE("sex.s");
                cpu.asregs.regs[a] = (int) bv;
            } break;
            case 0x12: /* zex.b */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                signed char bv = cpu.asregs.regs[b];

                TRACE("zex.b");
                cpu.asregs.regs[a] = (int) bv & 0xff;
            } break;
            case 0x13: /* zex.s */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                signed short bv = cpu.asregs.regs[b];

                TRACE("zex.s");
                cpu.asregs.regs[a] = (int) bv & 0xffff;
            } break;
            case 0x14: /* umul.x */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                unsigned av = cpu.asregs.regs[a];
                unsigned bv = cpu.asregs.regs[b];
                unsigned long long r =
                    (unsigned long long) av * (unsigned long long) bv;

                TRACE("umul.x");
                cpu.asregs.regs[a] = r >> 32;
            } break;
            case 0x15: /* mul.x */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                unsigned av = cpu.asregs.regs[a];
                unsigned bv = cpu.asregs.regs[b];
                signed long long r =
                    (signed long long) av * (signed long long) bv;

                TRACE("mul.x");
                cpu.asregs.regs[a] = r >> 32;
            } break;
            case 0x16: /* bad */
            case 0x17: /* bad */
            case 0x18: /* bad */
            {
                opc = opcode;

                TRACE("SIGILL0");
                cpu.asregs.exception = SIGILL;
                break;
            }
            case 0x19: /* jsr */
            {
                unsigned int fn = cpu.asregs.regs[(inst >> 4) & 0xf];
                unsigned int sp = cpu.asregs.regs[1];

                TRACE("jsr");

                /* Save a slot for the static chain.  */
                sp -= 4;

                /* Push the return address.  */
                sp -= 4;
                wlat(mach, sp, pc + 2);

                /* Push the current frame pointer.  */
                sp -= 4;
                wlat(mach, sp, cpu.asregs.regs[0]);

                /* Uncache the stack pointer and set the fp & pc.  */
                cpu.asregs.regs[1] = sp;
                cpu.asregs.regs[0] = sp;
                pc = fn - 2;
            } break;
            case 0x1a: /* jmpa */
            {
                unsigned int tgt = EXTRACT_WORD(pc + 2);

                TRACE("jmpa");
                pc = tgt - 2;
            } break;
            case 0x1b: /* ldi.b (immediate) */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int val = EXTRACT_WORD(pc + 2);

                TRACE("ldi.b");
                cpu.asregs.regs[reg] = val;
                pc += 4;
            } break;
            case 0x1c: /* ld.b (register indirect) */
            {
                int src = inst & 0xf;
                int dest = (inst >> 4) & 0xf;
                int xv;

                TRACE("ld.b");
                xv = cpu.asregs.regs[src];
                cpu.asregs.regs[dest] = rbat(mach, xv);
            } break;
            case 0x1d: /* lda.b */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int addr = EXTRACT_WORD(pc + 2);

                TRACE("lda.b");
                cpu.asregs.regs[reg] = rbat(mach, addr);
                pc += 4;
            } break;
            case 0x1e: /* st.b */
            {
                int dest = (inst >> 4) & 0xf;
                int val = inst & 0xf;

                TRACE("st.b");
                wbat(mach, cpu.asregs.regs[dest], cpu.asregs.regs[val]);
            } break;
            case 0x1f: /* sta.b */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int addr = EXTRACT_WORD(pc + 2);

                TRACE("sta.b");
                wbat(mach, addr, cpu.asregs.regs[reg]);
                pc += 4;
            } break;
            case 0x20: /* ldi.s (immediate) */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int val = EXTRACT_WORD(pc + 2);

                TRACE("ldi.s");
                cpu.asregs.regs[reg] = val;
                pc += 4;
            } break;
            case 0x21: /* ld.s (register indirect) */
            {
                int src = inst & 0xf;
                int dest = (inst >> 4) & 0xf;
                int xv;

                TRACE("ld.s");
                xv = cpu.asregs.regs[src];
                cpu.asregs.regs[dest] = rsat(mach, xv);
            } break;
            case 0x22: /* lda.s */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int addr = EXTRACT_WORD(pc + 2);

                TRACE("lda.s");
                cpu.asregs.regs[reg] = rsat(mach, addr);
                pc += 4;
            } break;
            case 0x23: /* st.s */
            {
                int dest = (inst >> 4) & 0xf;
                int val = inst & 0xf;

                TRACE("st.s");
                wsat(mach, cpu.asregs.regs[dest], cpu.asregs.regs[val]);
            } break;
            case 0x24: /* sta.s */
            {
                int reg = (inst >> 4) & 0xf;
                unsigned int addr = EXTRACT_WORD(pc + 2);

                TRACE("sta.s");
                wsat(mach, addr, cpu.asregs.regs[reg]);
                pc += 4;
            } break;
            case 0x25: /* jmp */
            {
                int reg = (inst >> 4) & 0xf;

                TRACE("jmp");
                pc = cpu.asregs.regs[reg] - 2;
            } break;
            case 0x26: /* and */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av, bv;

                TRACE("and");
                av = cpu.asregs.regs[a];
                bv = cpu.asregs.regs[b];
                cpu.asregs.regs[a] = av & bv;
            } break;
            case 0x27: /* lshr */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av = cpu.asregs.regs[a];
                int bv = cpu.asregs.regs[b];

                TRACE("lshr");
                cpu.asregs.regs[a] = (unsigned) ((unsigned) av >> bv);
            } break;
            case 0x28: /* ashl */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av = cpu.asregs.regs[a];
                int bv = cpu.asregs.regs[b];

                TRACE("ashl");
                cpu.asregs.regs[a] = av << bv;
            } break;
            case 0x29: /* sub */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                unsigned av = cpu.asregs.regs[a];
                unsigned bv = cpu.asregs.regs[b];

                TRACE("sub");
                cpu.asregs.regs[a] = av - bv;
            } break;
            case 0x2a: /* neg */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int bv = cpu.asregs.regs[b];

                TRACE("neg");
                cpu.asregs.regs[a] = -bv;
            } break;
            case 0x2b: /* or */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av, bv;

                TRACE("or");
                av = cpu.asregs.regs[a];
                bv = cpu.asregs.regs[b];
                cpu.asregs.regs[a] = av | bv;
            } break;
            case 0x2c: /* not */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int bv = cpu.asregs.regs[b];

                TRACE("not");
                cpu.asregs.regs[a] = 0xffffffff ^ bv;
            } break;
            case 0x2d: /* ashr */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av = cpu.asregs.regs[a];
                int bv = cpu.asregs.regs[b];

                TRACE("ashr");
                cpu.asregs.regs[a] = av >> bv;
            } break;
            case 0x2e: /* xor */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av, bv;

                TRACE("xor");
                av = cpu.asregs.regs[a];
                bv = cpu.asregs.regs[b];
                cpu.asregs.regs[a] = av ^ bv;
            } break;
            case 0x2f: /* mul */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                unsigned av = cpu.asregs.regs[a];
                unsigned bv = cpu.asregs.regs[b];

                TRACE("mul");
                cpu.asregs.regs[a] = av * bv;
            } break;
            case 0x30: /* swi */
            {
                unsigned int inum = EXTRACT_WORD(pc + 2);

                TRACE("swi");
                /* Set the special registers appropriately.  */
                cpu.asregs.sregs[2] = 3; /* MOXIE_EX_SWI */
                cpu.asregs.sregs[3] = inum;
                switch (inum) {
                case 0x1: /* SYS_exit */
                {
                    cpu.asregs.exception = SIGQUIT;
                    break;
                }

                case 90: /* SYS_mmap */
                {
                    sim_mmap(mach);
                    break;
                }

                default:
                    break;
                }
                pc += 4;
            } break;
            case 0x31: /* div */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av = cpu.asregs.regs[a];
                int bv = cpu.asregs.regs[b];

                TRACE("div");
                cpu.asregs.regs[a] = av / bv;
            } break;
            case 0x32: /* udiv */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                unsigned int av = cpu.asregs.regs[a];
                unsigned int bv = cpu.asregs.regs[b];

                TRACE("udiv");
                cpu.asregs.regs[a] = (av / bv);
            } break;
            case 0x33: /* mod */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                int av = cpu.asregs.regs[a];
                int bv = cpu.asregs.regs[b];

                TRACE("mod");
                cpu.asregs.regs[a] = av % bv;
            } break;
            case 0x34: /* umod */
            {
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;
                unsigned int av = cpu.asregs.regs[a];
                unsigned int bv = cpu.asregs.regs[b];

                TRACE("umod");
                cpu.asregs.regs[a] = (av % bv);
            } break;
            case 0x35: /* brk */
                TRACE("brk");
                cpu.asregs.exception = SIGTRAP;
                pc -= 2; /* Adjust pc */
                break;
            case 0x36: /* ldo.b */
            {
                unsigned int addr = EXTRACT_OFFSET(pc + 2);
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;

                TRACE("ldo.b");
                addr += cpu.asregs.regs[b];
                cpu.asregs.regs[a] = rbat(mach, addr);
                pc += 2;
            } break;
            case 0x37: /* sto.b */
            {
                unsigned int addr = EXTRACT_OFFSET(pc + 2);
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;

                TRACE("sto.b");
                addr += cpu.asregs.regs[a];
                wbat(mach, addr, cpu.asregs.regs[b]);
                pc += 2;
            } break;
            case 0x38: /* ldo.s */
            {
                unsigned int addr = EXTRACT_OFFSET(pc + 2);
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;

                TRACE("ldo.s");
                addr += cpu.asregs.regs[b];
                cpu.asregs.regs[a] = rsat(mach, addr);
                pc += 2;
            } break;
            case 0x39: /* sto.s */
            {
                unsigned int addr = EXTRACT_OFFSET(pc + 2);
                int a = (inst >> 4) & 0xf;
                int b = inst & 0xf;

                TRACE("sto.s");
                addr += cpu.asregs.regs[a];
                wsat(mach, addr, cpu.asregs.regs[b]);
                pc += 2;
            } break;
            default:
                opc = opcode;
                TRACE("SIGILL1");
                cpu.asregs.exception = SIGILL;
                break;
            }
        }

        insts++;
        pc += 2;

        if (cpu_budget && (insts >= cpu_budget))
            break;

    } while (!cpu.asregs.exception);

    /* Hide away the things we've cached while executing.  */
    cpu.asregs.regs[PC_REGNO] = pc;
    cpu.asregs.insts += insts; /* instructions done ... */

    return cpu.asregs.exception;
}
