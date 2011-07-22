/***************************************************************************
 *   Copyright (C) 2011 by Julius Baxter                                   *
 *   julius@opencores.org                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jtag/jtag.h"
#include "register.h"
#include "algorithm.h"
#include "target.h"
#include "breakpoints.h"
#include "target_type.h"
#include "or1k_jtag.h"
#include "or1k.h"

#include "server/server.h"
#include "server/gdb_server.h"


static char* or1k_core_reg_list[] =
{
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", 
	"r9", "r10", "r11", "r12", "r13", "r14", "r15", "r16",
	"r17", "r18", "r19", "r20", "r21", "r22", "r23", "r24",
	"r25", "r26", "r27", "r28", "r29", "r30", "r31",
	"ppc", "npc", "sr"
};

struct or1k_core_reg 
or1k_core_reg_list_arch_info[OR1KNUMCOREREGS] =
  {
	  {0, 1024, NULL, NULL, },
	  {1, 1025, NULL, NULL},
	  {2, 1026, NULL, NULL},
	  {3, 1027, NULL, NULL},
	  {4, 1028, NULL, NULL},
	  {5, 1029, NULL, NULL},
	  {6, 1030, NULL, NULL},
	  {7, 1031, NULL, NULL},
	  {8, 1032, NULL, NULL},
	  {9, 1033, NULL, NULL},
	  {10, 1034, NULL, NULL},
	  {11, 1035, NULL, NULL},
	  {12, 1036, NULL, NULL},
	  {13, 1037, NULL, NULL},
	  {14, 1038, NULL, NULL},
	  {15, 1039, NULL, NULL},
	  {16, 1040, NULL, NULL},
	  {17, 1041, NULL, NULL},
	  {18, 1042, NULL, NULL},
	  {19, 1043, NULL, NULL},
	  {20, 1044, NULL, NULL},
	  {21, 1045, NULL, NULL},
	  {22, 1046, NULL, NULL},
	  {23, 1047, NULL, NULL},
	  {24, 1048, NULL, NULL},
	  {25, 1049, NULL, NULL},
	  {26, 1050, NULL, NULL},
	  {27, 1051, NULL, NULL},
	  {28, 1052, NULL, NULL},
	  {29, 1053, NULL, NULL},
	  {30, 1054, NULL, NULL},
	  {31, 1055, NULL, NULL},
	  {32, 18, NULL, NULL},
	  {33, 16, NULL, NULL},
	  {34, 17, NULL, NULL},
  };


static int or1k_read_core_reg(struct target *target, int num);
static int or1k_write_core_reg(struct target *target, int num);

int or1k_save_context(struct target *target)
{

	LOG_DEBUG(" - ");
	int retval, i;
	struct or1k_common *or1k = target_to_or1k(target);

	/*
	retval = or1k_jtag_read_regs(&or1k->jtag, or1k->core_regs);
	if (retval != ERROR_OK)
		return retval;
	*/
	
	for (i = 0; i < OR1KNUMCOREREGS; i++)
	{
		if (!or1k->core_cache->reg_list[i].valid)
		{
			retval = or1k_jtag_read_cpu(&or1k->jtag, 
				   /* or1k spr address is in second field of
				      or1k_core_reg_list_arch_info
				   */
				   or1k_core_reg_list_arch_info[i].spr_num,
				   &or1k->core_regs[i]);
			/* Switch endianness of data just read */
			h_u32_to_be((uint8_t*) &or1k->core_regs[i], 
				    or1k->core_regs[i]);
			

			if (retval != ERROR_OK)
				return retval;
			
			/* We've just updated the core_reg[i], now update
			   the core cache */
			or1k_read_core_reg(target, i);
		}
	}

	return ERROR_OK;
}

int or1k_restore_context(struct target *target)
{
	int i;

	LOG_DEBUG(" - ");

	/* get pointers to arch-specific information */
	struct or1k_common *or1k = target_to_or1k(target);

	for (i = 0; i < OR1KNUMCOREREGS; i++)
	{
		if (or1k->core_cache->reg_list[i].dirty)
		{
			or1k_write_core_reg(target, i);
		}
	}

	/* write core regs */
	or1k_jtag_write_regs(&or1k->jtag, or1k->core_regs);

	return ERROR_OK;
}

static int or1k_read_core_reg(struct target *target, int num)
{
	uint32_t reg_value;

	/* get pointers to arch-specific information */
	struct or1k_common *or1k = target_to_or1k(target);

	if ((num < 0) || (num >= OR1KNUMCOREREGS))
		return ERROR_INVALID_ARGUMENTS;

	reg_value = or1k->core_regs[num];
	buf_set_u32(or1k->core_cache->reg_list[num].value, 0, 32, reg_value);
	LOG_DEBUG("read core reg %i value 0x%" PRIx32 "", num , reg_value);
	or1k->core_cache->reg_list[num].valid = 1;
	or1k->core_cache->reg_list[num].dirty = 0;

	return ERROR_OK;
}

static int or1k_write_core_reg(struct target *target, int num)
{
	uint32_t reg_value;

	/* get pointers to arch-specific information */
	struct or1k_common *or1k = target_to_or1k(target);

	if ((num < 0) || (num >= OR1KNUMCOREREGS))
		return ERROR_INVALID_ARGUMENTS;

	reg_value = buf_get_u32(or1k->core_cache->reg_list[num].value, 0, 32);
	or1k->core_regs[num] = reg_value;
	LOG_DEBUG("write core reg %i value 0x%" PRIx32 "", num , reg_value);
	or1k->core_cache->reg_list[num].valid = 1;
	or1k->core_cache->reg_list[num].dirty = 0;

	return ERROR_OK;
}

static int or1k_get_core_reg(struct reg *reg)
{
	int retval;
	struct or1k_core_reg *or1k_reg = reg->arch_info;
	struct target *target = or1k_reg->target;

	if (target->state != TARGET_HALTED)
	{
		return ERROR_TARGET_NOT_HALTED;
	}

	retval = or1k_read_core_reg(target, or1k_reg->list_num);

	return retval;
}

static int or1k_set_core_reg(struct reg *reg, uint8_t *buf)
{
	struct or1k_core_reg *or1k_reg = reg->arch_info;
	struct target *target = or1k_reg->target;
	uint32_t value = buf_get_u32(buf, 0, 32);

	if (target->state != TARGET_HALTED)
	{
		return ERROR_TARGET_NOT_HALTED;
	}

	buf_set_u32(reg->value, 0, 32, value);
	reg->dirty = 1;
	reg->valid = 1;

	return ERROR_OK;
}

static const struct reg_arch_type or1k_reg_type = {
	.get = or1k_get_core_reg,
	.set = or1k_set_core_reg,
};

static struct reg_cache *or1k_build_reg_cache(struct target *target)
{
	int num_regs = OR1KNUMCOREREGS;
	struct or1k_common *or1k = target_to_or1k(target);
	struct reg_cache **cache_p = register_get_last_cache_p(&target->reg_cache);
	struct reg_cache *cache = malloc(sizeof(struct reg_cache));
	struct reg *reg_list = malloc(sizeof(struct reg) * num_regs);
	struct or1k_core_reg *arch_info = 
		malloc(sizeof(struct or1k_core_reg) * num_regs);
	int i;

	/* Build the process context cache */
	cache->name = "openrisc 1000 registers";
	cache->next = NULL;
	cache->reg_list = reg_list;
	cache->num_regs = num_regs;
	(*cache_p) = cache;
	or1k->core_cache = cache;

	for (i = 0; i < num_regs; i++)
	{
		arch_info[i] = or1k_core_reg_list_arch_info[i];
		arch_info[i].target = target;
		arch_info[i].or1k_common = or1k;
		reg_list[i].name = or1k_core_reg_list[i];
		reg_list[i].size = 32;
		reg_list[i].value = calloc(1, 4);
		reg_list[i].dirty = 0;
		reg_list[i].valid = 0;
		reg_list[i].type = &or1k_reg_type;
		reg_list[i].arch_info = &arch_info[i];
	}

	return cache;
}


static int or1k_debug_entry(struct target *target)
{

  	/* Perhaps do more debugging entry (processor stalled) set up here */

	LOG_DEBUG(" - ");

	or1k_save_context(target);

	return ERROR_OK;
}

static int or1k_halt(struct target *target)
{
	struct or1k_common *or1k = target_to_or1k(target);

	LOG_DEBUG("target->state: %s",
		  target_state_name(target));

	if (target->state == TARGET_HALTED)
	{
		LOG_DEBUG("target was already halted");
		return ERROR_OK;
	}

	if (target->state == TARGET_UNKNOWN)
	{
		LOG_WARNING("target was in unknown state when halt was requested");
	}

	if (target->state == TARGET_RESET)
	{
		if ((jtag_get_reset_config() & RESET_SRST_PULLS_TRST) && 
		    jtag_get_srst())
		{
			LOG_ERROR("can't request a halt while in reset if nSRST pulls nTRST");
			return ERROR_TARGET_FAILURE;
		}
		else
		{
			target->debug_reason = DBG_REASON_DBGRQ;

			return ERROR_OK;
		}
	}

	/* Mohor debug unit-specific. */
	or1k_jtag_write_cpu_cr(&or1k->jtag, 1, 0);

	target->debug_reason = DBG_REASON_DBGRQ;

	return ERROR_OK;
}

static int or1k_poll(struct target *target)
{
	uint32_t cpu_cr;
	int retval;
	struct or1k_common *or1k = target_to_or1k(target);

	/* Possible specific to Mohor debug interface - others may have to do
	 * something different here. 
	 */
	retval = or1k_jtag_read_cpu_cr(&or1k->jtag, &cpu_cr);
	if (retval != ERROR_OK)
	{
		/* Try once to restart the JTAG infrastructure -
		   quite possibly the board has just been reset. */
		or1k_jtag_init(&or1k->jtag);

		retval = or1k_jtag_read_cpu_cr(&or1k->jtag, &cpu_cr);
		if (retval != ERROR_OK)
			return retval;

	}

	/* check for processor halted */
	if (cpu_cr & OR1K_MOHORDBGIF_CPU_CR_STALL)
	{
		if ((target->state == TARGET_RUNNING) || 
		    (target->state == TARGET_RESET))
		{
			target->state = TARGET_HALTED;

			if ((retval = or1k_debug_entry(target)) != ERROR_OK)
				return retval;

			target_call_event_callbacks(target, 
						    TARGET_EVENT_HALTED);
		}
		else if (target->state == TARGET_DEBUG_RUNNING)
		{
			target->state = TARGET_HALTED;

			if ((retval = or1k_debug_entry(target)) != ERROR_OK)
				return retval;

			target_call_event_callbacks(target, 
						    TARGET_EVENT_DEBUG_HALTED);
		}
	}
	else
	{
		
		/* If target was supposed to be stalled, stall it again */
		if  (target->state == TARGET_HALTED)
		{

			target->state = TARGET_RUNNING;
			
			or1k_halt(target);

			if ((retval = or1k_debug_entry(target)) != ERROR_OK)
				return retval;

			target_call_event_callbacks(target, 
						    TARGET_EVENT_DEBUG_HALTED);
		}
		
		/*
		  target->state = TARGET_RUNNING;
		*/
	}


	return ERROR_OK;
}

static int or1k_assert_reset(struct target *target)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_deassert_reset(struct target *target)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_soft_reset_halt(struct target *target)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_resume(struct target *target, int current,
		uint32_t address, int handle_breakpoints, int debug_execution)
{
	struct or1k_common *or1k = target_to_or1k(target);
	struct breakpoint *breakpoint = NULL;
	uint32_t resume_pc;
	int retval;

	if (target->state != TARGET_HALTED)
	{
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (!debug_execution)
	{
		target_free_all_working_areas(target);
		/*
		avr32_ap7k_enable_breakpoints(target);
		avr32_ap7k_enable_watchpoints(target);
		*/
	}

	/* current = 1: continue on current pc, otherwise continue at <address> */
	if (!current)
	{
#if 0
		if (retval != ERROR_OK)
			return retval;
#endif
	}

	/* Not sure what we do here - just guessing we fish out the PC from the 
	 * register cache and continue - Julius
	 */
	resume_pc = 
		buf_get_u32(or1k->core_cache->reg_list[OR1K_REG_NPC].value, 
			    0, 32);
	or1k_restore_context(target);

	/* the front-end may request us not to handle breakpoints */
	if (handle_breakpoints)
	{
		/* Single step past breakpoint at current address */
		if ((breakpoint = breakpoint_find(target, resume_pc)))
		{
			LOG_DEBUG("unset breakpoint at 0x%8.8" PRIx32 "", breakpoint->address);
#if 0
			avr32_ap7k_unset_breakpoint(target, breakpoint);
			avr32_ap7k_single_step_core(target);
			avr32_ap7k_set_breakpoint(target, breakpoint);
#endif
		}
	}

	/* I presume this is unstall - Julius
	 */
	/*
	retval = avr32_ocd_clearbits(&ap7k->jtag, AVR32_OCDREG_DC,
			OCDREG_DC_DBR);
	*/
	
	/* Mohor debug if, clearing control register unstalls */
	retval = or1k_jtag_write_cpu_cr(&or1k->jtag, 0, 0);
	if (retval != ERROR_OK)
		return retval;

	target->debug_reason = DBG_REASON_NOTHALTED;

	/* registers are now invalid */
	register_cache_invalidate(or1k->core_cache);

	if (!debug_execution)
	{
		target->state = TARGET_RUNNING;
		target_call_event_callbacks(target, TARGET_EVENT_RESUMED);
		LOG_DEBUG("target resumed at 0x%" PRIx32 "", resume_pc);
	}
	else
	{
		target->state = TARGET_DEBUG_RUNNING;
		target_call_event_callbacks(target, TARGET_EVENT_DEBUG_RESUMED);
		LOG_DEBUG("target debug resumed at 0x%" PRIx32 "", resume_pc);
	}

	return ERROR_OK;
}

static int or1k_step(struct target *target, int current,
		uint32_t address, int handle_breakpoints)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_add_breakpoint(struct target *target, 
			       struct breakpoint *breakpoint)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_remove_breakpoint(struct target *target,
				  struct breakpoint *breakpoint)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_add_watchpoint(struct target *target, 
			       struct watchpoint *watchpoint)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_remove_watchpoint(struct target *target,
				  struct watchpoint *watchpoint)
{
	LOG_ERROR("%s: implement me", __func__);

	return ERROR_OK;
}

static int or1k_read_memory(struct target *target, uint32_t address,
		uint32_t size, uint32_t count, uint8_t *buffer)
{
	struct or1k_common *or1k = target_to_or1k(target);

	LOG_DEBUG("address: 0x%8.8" PRIx32 ", size: 0x%8.8" PRIx32 ", count: 0x%8.8" PRIx32 "", address, size, count);

	if (target->state != TARGET_HALTED)
	{
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* sanitize arguments */
	if (((size != 4) && (size != 2) && (size != 1)) || (count == 0) || !(buffer))
		return ERROR_INVALID_ARGUMENTS;

	if (((size == 4) && (address & 0x3u)) || ((size == 2) && (address & 0x1u)))
		return ERROR_TARGET_UNALIGNED_ACCESS;

	switch (size)
	{
	case 4:
		return or1k_jtag_read_memory32(&or1k->jtag, address, count, (uint32_t*)(void *)buffer);
		break;
	case 2:
		return or1k_jtag_read_memory16(&or1k->jtag, address, count, (uint16_t*)(void *)buffer);
		break;
	case 1:
		return or1k_jtag_read_memory8(&or1k->jtag, address, count, buffer);
		break;
	default:
		break;
	}

	return ERROR_OK;
}

static int or1k_write_memory(struct target *target, uint32_t address,
		uint32_t size, uint32_t count, const uint8_t *buffer)
{
	struct or1k_common *or1k = target_to_or1k(target);

	LOG_DEBUG("address: 0x%8.8" PRIx32 ", size: 0x%8.8" PRIx32 ", count: 0x%8.8" PRIx32 "", address, size, count);

	if (target->state != TARGET_HALTED)
	{
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* sanitize arguments */
	if (((size != 4) && (size != 2) && (size != 1)) || (count == 0) || !(buffer))
		return ERROR_INVALID_ARGUMENTS;

	if (((size == 4) && (address & 0x3u)) || ((size == 2) && (address & 0x1u)))
		return ERROR_TARGET_UNALIGNED_ACCESS;

	switch (size)
	{
	case 4:
		return or1k_jtag_write_memory32(&or1k->jtag, address, count, (uint32_t*)(void *)buffer);
		break;
	case 2:
		return or1k_jtag_write_memory16(&or1k->jtag, address, count, (uint16_t*)(void *)buffer);
		break;
	case 1:
		return or1k_jtag_write_memory8(&or1k->jtag, address, count, buffer);
		break;
	default:
		break;
	}

	return ERROR_OK;
}


static int or1k_init_target(struct command_context *cmd_ctx,
		struct target *target)
{
	struct or1k_common *or1k = target_to_or1k(target);

	or1k->jtag.tap = target->tap;
	
	or1k_build_reg_cache(target);
	return ERROR_OK;
}

static int or1k_target_create(struct target *target, Jim_Interp *interp)
{
	struct or1k_common *or1k = calloc(1, sizeof(struct or1k_common));

	target->arch_info = or1k;

	return ERROR_OK;
}

static int or1k_examine(struct target *target)
{
	uint32_t cpu_cr;
	struct or1k_common *or1k = target_to_or1k(target);

	if (!target_was_examined(target))
	{
		target_set_examined(target);
		/* Do nothing special yet - Julius
		 */
		/*
		avr32_jtag_nexus_read(&ap7k->jtag, AVR32_OCDREG_DID, &devid);
		LOG_INFO("device id: %08x", devid);
		avr32_ocd_setbits(&ap7k->jtag, AVR32_OCDREG_DC,OCDREG_DC_DBE);
		avr32_jtag_nexus_read(&ap7k->jtag, AVR32_OCDREG_DS, &ds);
		*/
		/* check for processor halted */
	
		
		/* Possible specific to Mohor debug interface - others may 
		 * have to do something different here. 
		 */
 		or1k_jtag_read_cpu_cr(&or1k->jtag, &cpu_cr);
		if (cpu_cr & OR1K_MOHORDBGIF_CPU_CR_STALL) 
		{
			LOG_INFO("target is halted");
			target->state = TARGET_HALTED;
		}
		else
			target->state = TARGET_RUNNING;
	}

	return ERROR_OK;
}

static int or1k_bulk_write_memory(struct target *target, uint32_t address,
		uint32_t count, const uint8_t *buffer)
{
	struct or1k_common *or1k = target_to_or1k(target);
	
	LOG_DEBUG("address 0x%x count %d", address, count);

	/* Break it up into 256 byte blocks */

	uint32_t block_count_left = count & ~0x3;
	uint32_t block_count_address = address;
	uint8_t *block_count_buffer = (uint8_t*) buffer;

        uint32_t remain = count % 256;
	
	while (block_count_left > (64*4))
	{
		or1k_jtag_write_memory32(&or1k->jtag, 
					 block_count_address , 64,
					 (uint32_t*)block_count_buffer);

		block_count_left -= (64*4);
		block_count_address += (64*4);
		block_count_buffer += (64*4);
	}

	if (remain)
	{
		
		address += (count - remain);
		buffer += (count - remain);
		LOG_DEBUG("remaining %d bytes from address 0x%x",
			  remain, block_count_address);
		or1k_jtag_write_memory8(&or1k->jtag, block_count_address, 
					remain, (uint8_t *)block_count_buffer);
	}

	return ERROR_OK;
}


int or1k_arch_state(struct target *target)
{
  /*
	struct or1k_common *or1k = target_to_or1k(target);
  */
	/*
	LOG_USER("target halted due to %s, pc: 0x%8.8" PRIx32 "",
                debug_reason_name(target), ap7k->jtag.dpc);
	*/
   	return ERROR_OK;
}

int or1k_get_gdb_reg_list(struct target *target, struct reg **reg_list[], 
			  int *reg_list_size)
{
	struct or1k_common *or1k = target_to_or1k(target);
	int i;

	LOG_DEBUG(" - ");

	/* We will have this called whenever GDB connects. */
	or1k_save_context(target);
	
	*reg_list_size = OR1KNUMCOREREGS;
	*reg_list = malloc(sizeof(struct reg*) * (*reg_list_size));

	for (i = 0; i < OR1KNUMCOREREGS; i++)
		(*reg_list)[i] = &or1k->core_cache->reg_list[i];

	return ERROR_OK;

}

/* defined in server/gdb_server.h" */
extern struct connection *current_rsp_connection;
extern int gdb_rsp_resp_error;
#define ERROR_OK_NO_GDB_REPLY (-42)

COMMAND_HANDLER(or1k_readspr_command_handler)
{
	struct target *target = get_current_target(CMD_CTX);
	struct or1k_common *or1k = target_to_or1k(target);
	uint32_t regnum, regval;
	int retval, i;
	int reg_index = -1;

	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	
	//COMMAND_PARSE_NUMBER(u32, CMD_ARGV[0], regnum);

	if (1 != sscanf(CMD_ARGV[0], "%8x", &regnum))
		return ERROR_COMMAND_SYNTAX_ERROR;

	//LOG_DEBUG("adr 0x%08x",regnum);


	/* See if this register is in our cache and valid */
	
	struct or1k_core_reg *arch_info;
	for(i = 0; i < OR1KNUMCOREREGS; i++)
	{
		arch_info = (struct or1k_core_reg *)
			or1k->core_cache->reg_list[i].arch_info;
		if (arch_info->spr_num == regnum)
		{
			/* Reg is part of our cache. */
			reg_index = i;
			/* Is the local copy currently valid ? */
			if (or1k->core_cache->reg_list[i].valid == 1)
			{	
				regval = buf_get_u32(or1k->core_cache->reg_list[i].value,
						     0, 32);
			}
			
			break;
		}
	}

	/* Reg was not found in cache, or it was and the value wasn't valid. */
	if (reg_index == -1 ||
	    (reg_index != -1 && !(or1k->core_cache->reg_list[reg_index].valid ==
				  1)))
	{
		/* Now get the register value via JTAG */
		retval = or1k_jtag_read_cpu(&or1k->jtag, regnum, &regval);
		
		if (retval != ERROR_OK)
			return retval;
		
		/* Switch endianness of data just read */
		h_u32_to_be((uint8_t*) &regval, regval);
	}
	
	
	if (current_rsp_connection != NULL)
	{
		char gdb_reply[9];
		sprintf(gdb_reply, "%8x", (unsigned int) regval);
		gdb_reply[8] = 0x0;
		
		//LOG_INFO("%s",gdb_reply);

		char *hex_buffer;
		int bin_size;
		
		bin_size = strlen(gdb_reply);
		
		hex_buffer = malloc(bin_size*2 + 1);
		if (hex_buffer == NULL)
			return ERROR_GDB_BUFFER_TOO_SMALL;
		
		for (i = 0; i < bin_size; i++)
			snprintf(hex_buffer + i*2, 3, "%2.2x", gdb_reply[i]);
		hex_buffer[bin_size*2] = 0;
		
		gdb_put_packet(current_rsp_connection, hex_buffer, 
			       bin_size*2);
		
		free(hex_buffer);
		
		gdb_rsp_resp_error = ERROR_OK_NO_GDB_REPLY;
	} 



	/* Reg part of local core cache, but not valid, so update it */
	if (reg_index != -1 && !(or1k->core_cache->reg_list[reg_index].valid==1))
	{
		/* Set the value in the core reg array */
		or1k->core_regs[reg_index] = regval;
		/* Always update the register struct's value from the core 
		   array */
		or1k_read_core_reg(target, reg_index);
	}


	return ERROR_OK;
}

COMMAND_HANDLER(or1k_writespr_command_handler)
{
	struct target *target = get_current_target(CMD_CTX);
	struct or1k_common *or1k = target_to_or1k(target);
	uint32_t regnum, regval, regval_be;
	int retval;

	if (CMD_ARGC != 2)
		return ERROR_COMMAND_SYNTAX_ERROR;

	if (1 != sscanf(CMD_ARGV[0], "%8x", &regnum))
		return ERROR_COMMAND_SYNTAX_ERROR;
	
	if (1 != sscanf(CMD_ARGV[1], "%8x", &regval))
		return ERROR_COMMAND_SYNTAX_ERROR;

	LOG_DEBUG("adr 0x%08x val 0x%08x",regnum, regval);

	/* Switch endianness of data just read */
	h_u32_to_be((uint8_t*) &regval_be, regval);
#if 0
	uint32_t verify_regval;

	while(1){

	/* Now set the register via JTAG */
	retval = or1k_jtag_write_cpu(&or1k->jtag, regnum, regval_be);

	if (retval != ERROR_OK)
		return retval;

	/* Now read back the register via JTAG */
	retval = or1k_jtag_read_cpu(&or1k->jtag, regnum, &verify_regval);

	if (retval != ERROR_OK)
		return retval;

	LOG_DEBUG("written: %08x read: %08x",regval_be, verify_regval);
	
	if (regval_be == verify_regval)
		break;
	}

#else
	/* Now set the register via JTAG */
	retval = or1k_jtag_write_cpu(&or1k->jtag, regnum, regval_be);
	
	if (retval != ERROR_OK)
		return retval;
#endif	

	/* See if this reg is part of the core cache, if so update it */
	int i;
	struct or1k_core_reg *arch_info;
	for(i = 0; i < OR1KNUMCOREREGS; i++)
	{
		arch_info = (struct or1k_core_reg *)
			or1k->core_cache->reg_list[i].arch_info;
		if (arch_info->spr_num == regnum)
			break;
	}

	
	if (i < OR1KNUMCOREREGS)
	{
		/* Set the value in the core reg array */
		or1k->core_regs[i] = regval;
		/* Update the register struct's value from the core array */
		or1k_read_core_reg(target, i);
	}


	return ERROR_OK;
}




static const struct command_registration or1k_spr_command_handlers[] = {
	{
		"readspr",
		.handler = or1k_readspr_command_handler,
		.mode = COMMAND_ANY,
		.usage = "sprnum",
		.help = "read OR1k special purpose register sprnum",
	},
	{
		"writespr",
		.handler = or1k_writespr_command_handler,
		.mode = COMMAND_ANY,
		.usage = "sprnum value",
		.help = "write value to OR1k special purpose register sprnum",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration or1k_command_handlers[] = {
	{
		.chain = or1k_spr_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};


struct target_type or1k_target =
{
	.name = "or1k",

	.poll = or1k_poll,
	.arch_state = or1k_arch_state,

	.target_request_data = NULL,

	.halt = or1k_halt,
	.resume = or1k_resume,
	.step = or1k_step,

	.assert_reset = or1k_assert_reset,
	.deassert_reset = or1k_deassert_reset,
	.soft_reset_halt = or1k_soft_reset_halt,

	.get_gdb_reg_list = or1k_get_gdb_reg_list,

	.read_memory = or1k_read_memory,
	.write_memory = or1k_write_memory,
	.bulk_write_memory = or1k_bulk_write_memory,
	// .checksum_memory = or1k_checksum_memory,
	// .blank_check_memory = or1k_blank_check_memory,

	// .run_algorithm = or1k_run_algorithm,

	.commands = or1k_command_handlers,
	.add_breakpoint = or1k_add_breakpoint,
	.remove_breakpoint = or1k_remove_breakpoint,
	.add_watchpoint = or1k_add_watchpoint,
	.remove_watchpoint = or1k_remove_watchpoint,

	.target_create = or1k_target_create,
	.init_target = or1k_init_target,
	.examine = or1k_examine,
};