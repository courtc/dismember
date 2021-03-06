#include "guiproxy.h"
#include <stdexcept>
#include <assert.h>
#include "documentwindow.h"
#include "codeviewcanvas.h"

#include "memsegment.h"
#include "memlocdata.h"

GuiProxy::GuiProxy(Trace * ctx, DocumentWindow * gui) : m_ctx(ctx), m_gui(gui), m_dirty(true), m_lc(0), m_last_look_set(false)
{
	Callback<MemlocData*, GuiProxy> *cb = new Callback<MemlocData*, GuiProxy>;
	cb->setClass(this);
	cb->setCallback(&GuiProxy::UpdateMemloc);
	m_ctx->registerMemlocHook(cb);
}

line_ind_t GuiProxy::get_line_count()
{
	
	printf("Linecount: %d\n", m_lc);
	return m_lc;
}

uint8_t GuiProxy::get_line_rows(line_ind_t line)
{
	
	assert(line < m_lc);
	
	return 1;
}

void GuiProxy::UpdateMemloc(MemlocData *s, HookChange hc)
{
	
	// later, do this on the fly
	m_dirty = true;
	
}

address_t GuiProxy::get_addr_for_line(line_ind_t line)
{
	
	assert(line < m_lc);
	
	// Lookup the block after
	lclookup_i x = m_lclookup.upper_bound(line);
	if (x != m_lclookup.begin())
	{
		// then seek back
		x--;
		
		line_ind_t lca = (*x).first;
		struct addrblock *a = (*x).second;
		
		//printf("looking for line %d, found %d - %d\n", line, lca, lca+a->lc);
		if (lca  + a->lc > line)
		{
			address_t look = a->start;
			
			// We frequently query successively, so speedup by caching the position of the last line
			// if we're looking for that line, or after, and its within the block
			if (m_last_look_set && m_last_look_line >= lca && 
				m_last_look_line <= line && m_last_look_line < lca + a->lc)
			{
				// Then cache
				look = m_last_look_addr;
				lca = m_last_look_line;
			}
			
			
			// now we iterate to find the exact line
			while (lca < line)
			{
				MemlocData * d  = m_ctx->lookup_memloc(look);
				
				if (d)
					look += d->get_length();
				else
					//HACK HACK HACK - this will depend on the default datatype
					look ++;
				
				// HACK HACK HACK - this will depend on how many lines this datatype takes up
				lca ++;
			}
			// cache the last lookup line
			m_last_look_set = true;
			m_last_look_line = line;
			m_last_look_addr = look;
			return look;
		}
		
		printf("addr not in block - block start %llx end %llx len %llx lca %d line %d\n", a->start, a->start + a->lc, a->len, lca, line);
	}

	printf("Looking up invalid line %d\n", line);
	throw std::out_of_range("bad line addr!\n");
}

line_ind_t GuiProxy::get_line_for_addr(address_t addr)
{
	
	line_ind_t lca = 0;
	
	for (blk_i it = m_blocks.begin(); it != m_blocks.end(); it++)
	{
		struct addrblock * a = *it;
		
		if (a->start <= addr &&  a->start + a->len > addr)
		{
			address_t look = a->start;
			
			// now we iterate to find the exact line
			while (look < addr)
			{
				MemlocData * d  = m_ctx->lookup_memloc(look);
				if (d)
					look += d->get_length();
				else
					// HACK HACK HACK
					look++;
				
				lca ++;
			}
			return lca;
		}
		
		lca += a->lc;
	}
	
	printf("Looking up invalid addr %llx\n", addr);
	throw std::out_of_range("bad line addr!\n");
}


void GuiProxy::update(void)
{
	if (!m_dirty)
		return;
		
	m_lc = 0;
	m_last_look_set = false;
	
	//printf("Updatecalled\n");
	// empty the list
	for (blk_i it = m_blocks.begin(); it != m_blocks.end(); it++)
		delete  *it;
	
	m_blocks.clear();
	m_lclookup.clear();
	struct addrblock * cins = NULL;
	
	MemlocManager::memloclist_ci li = m_ctx->memloc_list_begin();
	u32 sblc = m_lc;
	
	for (MemSegmentManager::memseglist_ci mi = m_ctx->memsegs_begin(); mi != m_ctx->memsegs_end(); mi++)
	{
		// for each segment
		u32 memind = (*mi)->get_start();
		u32 memlast = (*mi)->get_length() + memind;
		
		while (memind < memlast)
		{
			if (cins == NULL)
			{
				cins = new struct addrblock();
				cins -> lc = 0;
				cins -> start = memind;
				sblc = m_lc;
			}
			
			while (li != m_ctx->memloc_list_end() && (*li).first < memind)
			{
				li++;
			}
			
			if (li != m_ctx->memloc_list_end() && (*li).first == memind)
			{
				
				MemlocData * d = NULL;
				d = (*li).second;
				if (d != NULL)
					memind += d->get_length();
				else
					// HACK HACK HACK - gui + this need to be integrated based on the size of the default view.
					memind ++;
				
			} else {
				memind += 1;
			}
			
			
			// Change based on line count
			m_lc ++;
			cins->lc++;
			
			s32 offs = memind - cins->start;
			
			if (offs > GPROX_BLOCK_SIZE)
			{
				m_lclookup[sblc] = cins;
				cins->len = offs;
				m_blocks.push_back(cins);
				cins = NULL;
			}
		}
		m_lclookup[sblc] = cins;
		cins->len =  memind - cins->start;
		m_blocks.push_back(cins);
		cins = NULL;
	}
	
	m_dirty = false;
}
