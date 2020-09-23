buddy_alloc
循环判断
1.获取当前空闲链表
2.查询链表到非空，空则继续下一循环下一条链表
3.通过free_list.next指向页结构体的链表成员，获取页结构体地址
4.删除该页
5.页的阶数清0，并清除页的PG_buddy标志
6.当前空闲链表空闲块数减1
7.进入分裂块函数，把空闲块后一半插入到下一个链表，前一半不变(为后续分配使用)
8.进入分配块属性设置函数：设置分配块头页和其他页的flag和order/firstpage
循环结束

buddy_free_pages
1.检查页是否已分配，已分配就清除信息，未分配则返回
循环判断 
2.先得到伙伴地址，检查伙伴是否未分配，已分配则退出循环，未分配则继续执行
3.删除伙伴在空闲链表，并链表空闲块数-1，同时清除伙伴信息
4.融合伙伴赋值给页，并阶数+1
退出循环
5.设置页信息，添加到空闲链表，并空闲块数+1



问题：其他页的flag没有标记。。。。
改动:创建新函数
static inline void set_page_order_head(struct page *page, u64 order)
static inline void clear_page_order_head(struct page *page)
static inline bool check_head(struct page *page, u64 order)
static void set_allocated_pages(struct page *page,u64 order)

static struct page *__alloc_page(struct global_mem *zone, u64 order)
struct page *buddy_get_pages(struct global_mem *zone, u64 order)
void buddy_free_pages(struct global_mem *zone, struct page *page)


切换位^
检查位&
开位|
关位&~
void 用return;或不用
void *需要返回指针
