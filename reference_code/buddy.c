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

/* �������ҳ������ */
static void prepare_compound_pages(struct page *page, unsigned long order)//���÷�����п�����ԣ���ҳͷ������ҳ��flag��order/firstpage
{
    unsigned long i;
    unsigned long nr_pages = (1UL<<order);

    // �׸�page��¼���ҳ��orderֵ
    set_compound_order(page, order);//������ҳ�Ľ���
    __SetPageHead(page); // ��ҳ����head��־
    for(i = 1; i < nr_pages; i++)
    {
        struct page *p = page + i;
        __SetPageTail(p); // ����ҳ����tail��־
        p->first_page = page;//��������ҳ��firstflag
    }
}

/* �����ҳ���з��ѣ��Ի�������С��ҳ */
static void expand(struct mem_zone *zone, struct page *page,
                   unsigned long low_order, unsigned long high_order,
                   struct free_area *area)//�����ǰѿ��п��һ����뵽��һ������ǰһ�벻��(Ϊ��������ʹ��)
{
    unsigned long size = (1U << high_order);
    while (high_order > low_order)
    {
        area--;//
        high_order--;
        size >>= 1;
        list_add(&page[size].lru, &area->free_list);
		//һ���ڴ���кܶ�ҳ��ȡ�ڴ����м�ҳ���
		//ҳ����������һ��ģ�����page[size]�൱��*(page+size),��ȡ�ø�ҳ����м�ҳ
        area->nr_free++;
        // set page order
        set_page_order_buddy(&page[size], high_order);
		//�趨ҳ�Ľ�����flagΪPG_buudy
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
        area = zone->free_area + current_order;//��ȡ��ǰ��������
        if (list_empty(&area->free_list)) {
            continue;
        }//��������Ϊ����������ֱ��Ϊ�ǿ�
        // remove closest size page
        page = list_entry(area->free_list.next, struct page, lru);
		//area->free_list.nextָ���һ��ҳ�ṹ���lru
		//��ʾ����area->free_list.nex���������Ǹ�������ָ�룬������ȥ�Լ��������е�ƫ������ֵ��Ӧ�õõ�������ָ��
		//area->free_list�������ҳ�ṹ����ֻ�ǹ����ߣ����ǲ�����
        list_del(&page->lru);//ɾ��������
        rmv_page_order_buddy(page);//������0�������PG_buddy��־
        area->nr_free--;//���ٹ����ߵĿ��п�����
        // expand to lower order
        expand(zone, page, order, current_order, area);////�����ǰѿ��п��һ����뵽��һ������ǰһ�벻��(Ϊ��������ʹ��)
        // compound page
		
		if (order > 0)
			prepare_compound_pages(page, order);//���÷�����п�����ԣ���ҳͷ������ҳ��flag��order/firstpage
		else // single page
			page->order = 0;
        return page;//�õ�ҳ
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
    }//�ж�ҳ�Ľ����Ƿ���ڵ������ҳ
	//ע�⽨��free_list�ĳߴ������ף�����ʵ��Ԫ��С��1�������ܴﵽ����
    //TODO: lock zone->lock
    page = __alloc_page(order, zone);//����ҳ
    //TODO: unlock zone->lock
    return page;
}

////////////////////////////////////////////////

/* �������ҳ */
static int destroy_compound_pages(struct page *page, unsigned long order)//�������ҳ�����ԣ�ͷҳ������ҳ��flag��order/firstflag���
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
        (page->flags & ((1UL<<PG_head)|(1UL<<PG_tail)))//����Ƿ��Ƿ���ҳ

#define page_is_buddy(page,order) \
        (PageBuddy(page) && (page->order == order))//����Ƿ���δ�����buddyҳ

void buddy_free_pages(struct mem_zone *zone,
                      struct page *page)
{
    unsigned long order = compound_order(page);
    unsigned long buddy_idx = 0, combinded_idx = 0;
    unsigned long page_idx = page - zone->first_page;
    //TODO: lock zone->lock
	if (PageCompound(page))//����Ƿ���ҳ�ѷ���
		if (destroy_compound_pages(page, order))////�������ҳ��ͷҳ������ҳ��flag//ע���ʱ��û�����order/firstpage
			BUDDY_BUG(__FILE__, __LINE__);
        
	//����1����ҳδ�������������Ӧ��ִ��
	//�жϽ����û��������
	//����������else��return�����ֽ������
	//ʵ��ԭ���ǣ���ִ�з�����ͷŵ�ʱ����ʲô���͵�ҳ��Ӧ���ͷţ������Ƿ��أ�����ԭ��Ӧ���ǳ�ʼ��ʱҳ��û�����ԣ�����ʹ����ҳ�ͷš�
    while (order < BUDDY_MAX_ORDER-1)
    {
        struct page *buddy;
        // find and delete buddy to combine
        buddy_idx = __find_buddy_index(page_idx, order);//��ȡ��������
        buddy = page + (buddy_idx - page_idx);//���ݻ��������ȡ����ַ
        if (!page_is_buddy(buddy, order))//�жϻ����δ����ľͼ���ִ��
            break;
        list_del(&buddy->lru);//ȡ�����
        zone->free_area[order].nr_free--;//����������п���Ŀ-1
        // remove buddy's flag and order
        rmv_page_order_buddy(buddy);//������Ľ�����flag
        // update page and page_idx after combined
        combinded_idx = __find_combined_index(page_idx, order);//��ȡ��Ϻ�Ŀ��п�����
        page = page + (combinded_idx - page_idx);//��ȡ��Ϻ�Ŀ��п���ҳ
        page_idx = combinded_idx;
        order++;//����+1
    }
	//����2��Ϊ��û�����page��buddy������ҳ��first page
	//�жϽ����û������
	//�������Ϊ����δ�����ҳ��ֻ��Ҫ�����־,���еĽ������������Ҫ���first page�������ڷ��ѿ麯��ʱ����û���޸�first page����Ϊ����δflag����tail��ҳû�����塣
    set_page_order_buddy(page, order);//������ҳ��flag�ͽ���
    list_add(&page->lru, &zone->free_area[order].free_list);//��ӿ��п鵽��������
    zone->free_area[order].nr_free++;//��������Ŀ��п���Ŀ+1
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
