#include "buddy.h"

void buddy_system_init(struct mem_zone *zone,
                       struct page *start_page,
                       unsigned long start_addr,
                       unsigned long page_num)
{
    unsigned long i;
    struct page *page = NULL;
    struct free_area *area = NULL;
    // init memory zone
    zone->page_num = page_num;
    zone->page_size = BUDDY_PAGE_SIZE;
    zone->first_page = start_page;
    zone->start_addr = start_addr;
    zone->end_addr = start_addr + page_num * BUDDY_PAGE_SIZE;
    // TODO: init zone->lock
    // init each area
    for (i = 0; i < BUDDY_MAX_ORDER; i++)
    {
        area = zone->free_area + i;
        INIT_LIST_HEAD(&area->free_list);
        area->nr_free = 0;
    }
    memset(start_page, 0, page_num * sizeof(struct page));
    // init and free each page
    for (i = 0; i < page_num; i++)
    {
        page = zone->first_page + i;
        INIT_LIST_HEAD(&page->lru);
        // TODO: init page->lock
        buddy_free_pages(zone, page);
    }
}

/* 设置组合页的属性 */
static void prepare_compound_pages(struct page *page, unsigned long order)//设置分配空闲块的属性，即页头和其他页的flag和order/firstpage
{
    unsigned long i;
    unsigned long nr_pages = (1UL<<order);

    // 首个page记录组合页的order值
    set_compound_order(page, order);//设置首页的阶数
    __SetPageHead(page); // 首页设置head标志
    for(i = 1; i < nr_pages; i++)
    {
        struct page *p = page + i;
        __SetPageTail(p); // 其余页设置tail标志
        p->first_page = page;//设置其他页的firstflag
    }
}

/* 将组合页进行分裂，以获得所需大小的页 */
static void expand(struct mem_zone *zone, struct page *page,
                   unsigned long low_order, unsigned long high_order,
                   struct free_area *area)//作用是把空闲块后一半插入到下一个链表，前一半不变(为后续分配使用)
{
    unsigned long size = (1U << high_order);
    while (high_order > low_order)
    {
        area--;//
        high_order--;
        size >>= 1;
        list_add(&page[size].lru, &area->free_list);
		//一个内存块有很多页，取内存块的中间页添加
		//页都是连接在一起的，这里page[size]相当于*(page+size),即取得该页块的中间页
        area->nr_free++;
        // set page order
        set_page_order_buddy(&page[size], high_order);
		//设定页的阶数和flag为PG_buudy
    }
}

static struct page *__alloc_page(unsigned long order,
                                 struct mem_zone *zone)
{
    struct page *page = NULL;
    struct free_area *area = NULL;
    unsigned long current_order = 0;

    for (current_order = order;
         current_order < BUDDY_MAX_ORDER; current_order++)
    {
        area = zone->free_area + current_order;//获取当前空闲链表
        if (list_empty(&area->free_list)) {
            continue;
        }//空闲链表为空则跳过，直到为非空
        // remove closest size page
        page = list_entry(area->free_list.next, struct page, lru);
		//area->free_list.next指向第一个页结构体的lru
		//表示利用area->free_list.nex找容器的那个变量的指针，把它减去自己在容器中的偏移量的值就应该得到容器的指针
		//area->free_list管理空闲页结构链表，只是管理者，不是参与者
        list_del(&page->lru);//删除参与者
        rmv_page_order_buddy(page);//阶数置0，并清除PG_buddy标志
        area->nr_free--;//减少管理者的空闲块数量
        // expand to lower order
        expand(zone, page, order, current_order, area);////作用是把空闲块后一半插入到下一个链表，前一半不变(为后续分配使用)
        // compound page
		
		if (order > 0)
			prepare_compound_pages(page, order);//设置分配空闲块的属性，即页头和其他页的flag和order/firstpage
		else // single page
			page->order = 0;
        return page;//得到页
    }
    return NULL;
}

struct page *buddy_get_pages(struct mem_zone *zone,
                             unsigned long order)
{
    struct page *page = NULL;

    if (order >= BUDDY_MAX_ORDER)
    {
        BUDDY_BUG(__FILE__, __LINE__);
        return NULL;
    }//判断页的阶数是否大于等于最高页
	//注意建立free_list的尺寸是最大阶，数组实际元素小于1，即不能达到最大阶
    //TODO: lock zone->lock
    page = __alloc_page(order, zone);//分配页
    //TODO: unlock zone->lock
    return page;
}

////////////////////////////////////////////////

/* 销毁组合页 */
static int destroy_compound_pages(struct page *page, unsigned long order)//清除分配页的属性，头页和其他页的flag和order/firstflag清除
{
    int bad = 0;
    unsigned long i;
    unsigned long nr_pages = (1UL<<order);

    __ClearPageHead(page);
    for(i = 1; i < nr_pages; i++)
    {
        struct page *p = page + i;
        if( !PageTail(p) || p->first_page != page )
        {
            bad++;
            BUDDY_BUG(__FILE__, __LINE__);
        }
        __ClearPageTail(p);
    }
    return bad;
}

#define PageCompound(page) \
        (page->flags & ((1UL<<PG_head)|(1UL<<PG_tail)))//检查是否是分配页

#define page_is_buddy(page,order) \
        (PageBuddy(page) && (page->order == order))//检查是否是未分配的buddy页

void buddy_free_pages(struct mem_zone *zone,
                      struct page *page)
{
    unsigned long order = compound_order(page);
    unsigned long buddy_idx = 0, combinded_idx = 0;
    unsigned long page_idx = page - zone->first_page;
    //TODO: lock zone->lock
	if (PageCompound(page))//检查是否是页已分配
		if (destroy_compound_pages(page, order))////清除分配页的头页和其他页的flag//注意此时并没有清除order/firstpage
			BUDDY_BUG(__FILE__, __LINE__);
        
	//问题1：当页未分配后续操作不应该执行
	//判断结果：没有有问题
	//错误解决：加else，return。发现结果错误
	//实际原因是：当执行分配的释放的时候不论什么类型的页都应该释放，而不是返回，具体原因应该是初始化时页并没有属性，但是使用了页释放。
    while (order < BUDDY_MAX_ORDER-1)
    {
        struct page *buddy;
        // find and delete buddy to combine
        buddy_idx = __find_buddy_index(page_idx, order);//获取伙伴的索引
        buddy = page + (buddy_idx - page_idx);//根据伙伴索引获取伙伴地址
        if (!page_is_buddy(buddy, order))//判断伙伴是未分配的就继续执行
            break;
        list_del(&buddy->lru);//取出伙伴
        zone->free_area[order].nr_free--;//空闲链表空闲块数目-1
        // remove buddy's flag and order
        rmv_page_order_buddy(buddy);//清除伙伴的阶数和flag
        // update page and page_idx after combined
        combinded_idx = __find_combined_index(page_idx, order);//获取组合后的空闲块索引
        page = page + (combinded_idx - page_idx);//获取组合后的空闲块首页
        page_idx = combinded_idx;
        order++;//阶数+1
    }
	//问题2：为何没有清除page和buddy的其他页的first page
	//判断结果：没有问题
	//解决：因为对于未分配的页，只需要清除标志,其中的阶已清除。不需要清除first page，比如在分裂块函数时，并没有修改first page，因为对于未flag不是tail的页没有意义。
    set_page_order_buddy(page, order);//设置首页的flag和阶数
    list_add(&page->lru, &zone->free_area[order].free_list);//添加空闲块到空闲链表
    zone->free_area[order].nr_free++;//空闲链表的空闲块数目+1
    //TODO: unlock zone->lock
}

////////////////////////////////////////////////

void *page_to_virt(struct mem_zone *zone,
                   struct page *page)
{
    unsigned long page_idx = 0;
    unsigned long address = 0;

    page_idx = page - zone->first_page;
    address = zone->start_addr + page_idx * BUDDY_PAGE_SIZE;

    return (void *)address;
}

struct page *virt_to_page(struct mem_zone *zone, void *ptr)
{
    unsigned long page_idx = 0;
    struct page *page = NULL;
    unsigned long address = (unsigned long)ptr;

    if((address<zone->start_addr)||(address>zone->end_addr))
    {
        printf("start_addr=0x%lx, end_addr=0x%lx, address=0x%lx\n",
                zone->start_addr, zone->end_addr, address);
        BUDDY_BUG(__FILE__, __LINE__);
        return NULL;
    }
    page_idx = (address - zone->start_addr)>>BUDDY_PAGE_SHIFT;

    page = zone->first_page + page_idx;
    return page;
}

unsigned long buddy_num_free_page(struct mem_zone *zone)
{
    unsigned long i, ret;
    for (i = 0, ret = 0; i < BUDDY_MAX_ORDER; i++)
    {
        ret += zone->free_area[i].nr_free * (1UL<<i);
    }
    return ret;
}
