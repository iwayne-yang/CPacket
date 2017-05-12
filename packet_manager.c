/*
 * CPacket mem test 
 * Buddy
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "my_log.h"
#include "packet_manager.h"

int m_nPages = PKT_KILOS_DEFAULT;
int m_nTypes;
uchar *m_pOriginBuffer;
uchar *m_pBuffer;
PBA m_PBAs[PKT_KILOS_MAX];
CPacket m_CPacketList[NALL];
CPacket *m_pFreeList;

int log2i(uint x)
{
	int r = 31;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

void DumpUsage()
{
	int type = 0;
	int freepages = 0;    
	PBN *current = NULL;
    
	LOG("m_nTypes=%d\n", m_nTypes);

    for(type = 0; type < m_nTypes; type++){
		if(m_PBAs[type].pHeader){
			current = m_PBAs[type].pHeader;
            LOG("m_PBAs[%d]*\n", type);
			while (1) {
				freepages += 1<<type;
				if(current == m_PBAs[type].pHeader->pPrev){
					break;
				}
				current = current->pNext;
			}
		} else {
            LOG("m_PBAs[%d]0\n", type);
        }
	}
    
	LOG("Packet usage : %dK / %dK, %d%%\n",
	    (m_nPages - freepages),
	    m_nPages, 
	    (100-100*freepages/m_nPages));
}

void DumpNodes(void)
{
	int type;
	PBN *current;
    int i, j;

#if 0 //m_nTypes ���ʱ������е���
	//��ӡ���нڵ��״̬
	printf(" nodes:-------------------------------------------------------------\n");
	for(type = 0; type < m_nTypes; type++){
        
        printf(" %4d(%2d^2) %09p : ", (1u <<type), type, m_PBAs[type].pHeader);
        
		for(i = 0; i < (1 << type) - 1; i++){
			printf(" ");
		}        
         
		for(i = 0; i < m_PBAs[type].nCount; i++){
            
			printf("%x", m_PBAs[type].pArray[i].pNext ? 1 : 0);
            
			for(j = 0; (j < (2 << type) - 1) && (i != m_PBAs[type].nCount - 1); j++){
				printf(" ");
			}
		}
       
		printf("\n");
	}
#endif    
	//��ӡ��������
	printf(" links:-------------------------------------------------------------\n");
	for(type = 0; type < m_nTypes; type++){//˳���ӡ
	
        printf(" %4d(%2d^2) %09p : ", (1u <<type), type, m_PBAs[type].pHeader);
        
		if(m_PBAs[type].pHeader){
            
			current = m_PBAs[type].pHeader;
            
			while (1) {
                
				printf(" %d ", current->nIndex);
                
				if(current == m_PBAs[type].pHeader->pPrev){
					break;
				}
                
				current = current->pNext;
			}
		}
		printf("\n");
	}        
	printf(" -------------------------------------------------------------------\n");
}

// ɾ�����нڵ�
inline void RemoveFree(PBN **pHeader, PBN *pThis)
{
	if (pThis == *pHeader) {
		*pHeader = pThis->pNext;
		if (pThis == *pHeader) {// ֻ��һ���ڵ�
			pThis->pNext = NULL;
			*pHeader = NULL;
			return;
		}
	}
    
    pThis->pPrev->pNext = pThis->pNext;
	pThis->pNext->pPrev = pThis->pPrev;
	pThis->pNext = NULL;
}

// ���µĿ��нڵ��������
inline void InsertFree(PBN **pHeader, PBN *pThis)
{
	if (*pHeader) {
		(*pHeader)->pPrev->pNext = pThis;
		pThis->pPrev = (*pHeader)->pPrev;
		(*pHeader)->pPrev = pThis;
		pThis->pNext = *pHeader;
		*pHeader = pThis;
	} else {
		*pHeader = pThis;
		pThis->pPrev = pThis;
		pThis->pNext = pThis;
	}
}

CPacket *AllocPacket(void)
{
	CPacket *p = m_pFreeList;
	if (p) {
		m_pFreeList = p->m_pNext;
	} else {
	    // �������ɶ���
		for (p = m_pFreeList = &m_CPacketList[NALL - 1]; m_CPacketList < p; p--) {
			p->m_pNext = p - 1;
		}
        
		(p + 1)->m_pNext = NULL;// ���һ�����ɶ���ĺ���Ϊ��
	}
	
	return p;
}

void FreePacket(CPacket* p)
{
	assert(p);
	p->m_pNext = m_pFreeList;
	m_pFreeList = p;
}

/*
 Buddy�㷨���ͷ�ԭ��
 
 �ڴ���ͷ��Ƿ��������̣�Ҳ���Կ����ǻ��ĺϲ����̡�
 ���ͷ�һ����ʱ���������Ӧ�������п����Ƿ��л����ڣ�
 ���û�л��飬��ֱ�Ӱ�Ҫ�ͷŵĿ��������ͷ��
 ����У����������ժ�»�飬�ϲ���һ����飬
 Ȼ���������ϲ���Ŀ��ڸ���һ���������Ƿ��л����ڣ�ֱ�����ܺϲ������Ѿ��ϲ��������Ŀ�(2*2*2*2*2*2*2*2*2��ҳ��)��

 */
void PutPacket(CPacket *pPacket)
{
	int index;
	int theother;
	int type;
	int pages;
	int i;
	int merged = 0; // 0-û�кϲ� 1-���ںϲ� 2-�ϲ����

	if(!pPacket){
		return;
	}

    LOG("m_pBuffer=%p m_Size=%d(Bytes)\n", pPacket->m_pBuffer, pPacket->m_Size);
    
	if(!pPacket->m_Size || !pPacket->m_pBuffer){
		FreePacket(pPacket);
		return;
	}

	// ȡ�������ݳ�Ա
	pages = pPacket->m_Size / PKT_PAGE_SIZE;
	index = (pPacket->m_pBuffer - m_pBuffer) / PKT_PAGE_SIZE;

	type = log2i(pages);
	if((1u << type) != (unsigned int)pages){
		type++;
		index += pages; // indexΪҪ�ͷŵİ��ڴ��������0���ڵ����
	}else{
		i = type;
		index /= pages; // indexΪ��ǰ�ڵ�����
		goto post_merge;
	}

	// ���������������������2�ݴνڵ㣬�� 11 = 8 + 2 + 1����С����һһ���кϲ�
	for(i = 0; i < type; i++){
		if(index & 0x1){
			if(merged == 0){
				if(m_PBAs[i].pArray[index].pNext){
					merged = 1;
					RemoveFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index]);
				}else{
					merged = 2;
					InsertFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index - 1]);
				}
			}else if(merged == 2){
				InsertFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index - 1]);
			}
		}else{
			if(merged == 1){
				if(m_PBAs[i].pArray[index + 1].pNext){
					RemoveFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index + 1]);
				}else{
					merged = 2;
					InsertFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index]);
				}
			}
		}
		index /= 2;
	}

	// ������С�ڵ�ϲ��ɴ�ڵ�, ��1->2->4->8, ֱ�������ٺϲ�Ϊֹ
	if(merged == 1){
post_merge:
		for(; i < m_nTypes - 1; i++){
			theother = (index % 2) ? (index - 1) : (index + 1);
			if(m_PBAs[i].pArray[theother].pNext){
				RemoveFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[theother]);
			}else{
				break;
			}
			index /= 2;
		}

		InsertFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index]);
	}

	// ���հ��Ƕ���
	FreePacket(pPacket);
}
/*
 Buddy�㷨�ķ���ԭ��
 ����ϵͳ��Ҫ4(2*2)��ҳ���С���ڴ�飬
 ���㷨�͵�free_area[2]�в��ң�����������п��п飬��ֱ�Ӵ���ժ�²������ȥ��
 ���û�У��㷨��˳���������ϲ���free_area[3],
 ���free_area[3]���п��п飬�����������ժ�£��ֳɵȴ�С�������֣�ǰ�ĸ�ҳ����Ϊһ�������free_area[2]����4��ҳ������ȥ��
 free_area[3]��Ҳû�У��������ϲ��ң����free_area[4]���У��ͽ���16(2*2*2*2)��ҳ��ȷֳ����ݣ�ǰһ�����free_area[3]������ͷ������һ���8��ҳ�ȷֳ����ȷ֣�ǰһ���free_area[2]
 �������У���һ������ȥ��
 ����free_area[4]Ҳû�У����ظ�����Ĺ��̣�ֱ������free_area�������������û����������䡣
 */
CPacket *GetPacket(uint dwBytes)
{
    LOG("dwByters=%d(Bytes)\n", dwBytes);
	CPacket *pPacket = NULL;
	int index = 0;
	int type = 0;
	int pages = 0;
	int i = 0;
    
	if(!(pPacket = AllocPacket())){
		LOG("AllocPacket error!\n");
		return NULL;
	}
    
    // init
    pPacket->m_RefCount = 1;
    pPacket->m_Length = 0;
    pPacket->m_Size = 0;
    pPacket->m_pBuffer = NULL;
    pPacket->m_iLastUId = -1;

    //LOG("Got package.\n");
    
	// û������ռ䣬ֻ���ؿհ�
	if(dwBytes == 0){
		pPacket->m_Size = 0;
		pPacket->m_pBuffer = NULL;
		return pPacket;
	}

	// ���������С������ڵ㼶��
	pages = (dwBytes + PKT_PAGE_SIZE - 1) / PKT_PAGE_SIZE;
	type = log2i(pages);
    
	if((1u << type) != (unsigned int)pages){
		type++;
	}
#if PKT_KILOS_BLOCK_BIG
	pages = 1 << type;
#endif
    //LOG("pages=%d type=%d m_nTypes=%d\n", pages, type, m_nTypes);
    
	// ������С�Ŀ��нڵ�
	for(i = type; i < m_nTypes; i++){
        //LOG("type[%d]: %p %p %d\n", i, m_PBAs[i].pHeader, m_PBAs[i].pArray, m_PBAs[i].nCount);
        if(m_PBAs[i].pHeader){
			break;
		}
	}
    
	if(i >= m_nTypes){
		LOG("GetPacket none free node !\n");
		FreePacket(pPacket);
		return NULL;
	}
    
    //LOG("Got node at %d\n", i);
    
	// ȡ����С�Ŀ��нڵ�
	index = m_PBAs[i].pHeader->nIndex;
    //LOG("i = %d index = %d\n", i, index);
	RemoveFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index]); 

    //LOG("[%d]:pHeader=%p, nCount=%d\n",i, m_PBAs[i].pHeader, m_PBAs[i].nCount);
    
	// �������ݳ�Ա
	pPacket->m_Size = pages * PKT_PAGE_SIZE;
	pPacket->m_pBuffer = m_pBuffer + (index << i) * PKT_PAGE_SIZE;

    //LOG("m_pBuffer=%p size=%d i=%d index=%d\n", pPacket->m_pBuffer, pPacket->m_Size, i, index);
    
	/*
	 * �����,�·ֿռ䣬�԰��֣�ֱ���ﵽ���󼶱�
	 */
	for (i--, index *= 2; i >= type; i--, index *= 2) {		
        //LOG("insert %d %d\n", i, index);
		InsertFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index + 1]);
	}
#if (!PKT_KILOS_BLOCK_BIG)
	/*
     * ����������Ŀռ�����ĸ�С�����нڵ�,��������11��ҳ��ʱ,
     * ʵ��ȡ������16��ҳ��,�����5 = 4 + 1��ҳ��
     */
	if((unsigned int)pages != (1u << type)){
        //LOG("pages=%d type=%d\n", pages, type);
		for(; i >= 0; i--){
			index = index * 2;
			if(!(pages & BITMSK(i))){
				InsertFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index + 1]);
			} else {
				index += 1;
				pages -= (1u << i);
				if(!pages){
					InsertFree(&m_PBAs[i].pHeader, &m_PBAs[i].pArray[index]);
					break;
				}
			}
		}
	}
#endif
	return pPacket;
}
/*
 �����ڴ桢����
 ��ʼ���ڴ�����
 */
void CPacket_init(void)
{
    int size, i;
    m_pOriginBuffer = (uchar *)malloc(sizeof(uchar)*(m_nPages + 1) * PKT_PAGE_SIZE);
    if(!m_pOriginBuffer) {
        LOG("malloc mem block failed.\n");
        return;
    }
    
    m_pBuffer = (uchar *)(((unsigned long)m_pOriginBuffer + PKT_PAGE_SIZE) & ~(PKT_PAGE_SIZE - 1));
    
    LOG("m_pOriginBuffer=%p, m_nPages=%d\n", m_pOriginBuffer, m_nPages);
    
	for (m_nTypes = 0, size = m_nPages; BITMSK(m_nTypes) <= m_nPages; m_nTypes++, size /= 2) {                
		// ȡż��,Ϊ�˽ڵ�ϲ�����
		m_PBAs[m_nTypes].nCount = ((size + 1 ) & 0xfffffffe);

        m_PBAs[m_nTypes].pArray = (PBN*) malloc (sizeof(PBN) * m_PBAs[m_nTypes].nCount);
		if (!m_PBAs[m_nTypes].pArray) {
			LOG("create new nodes failed !\n");
            return;
		}
        
		// ���нڵ��־���
		for (i = 0; i < m_PBAs[m_nTypes].nCount; i++) {
            m_PBAs[m_nTypes].pArray[i].pPrev = NULL;
			m_PBAs[m_nTypes].pArray[i].pNext = NULL;
			m_PBAs[m_nTypes].pArray[i].nIndex = i;
		}
        
		// ��ʼ�����нڵ�����
		if (m_nPages & BITMSK(m_nTypes)) {
			m_PBAs[m_nTypes].pHeader = &m_PBAs[m_nTypes].pArray[0];
			m_PBAs[m_nTypes].pHeader->pNext = m_PBAs[m_nTypes].pHeader;
            m_PBAs[m_nTypes].pHeader->pPrev = m_PBAs[m_nTypes].pHeader;
		} else {
			m_PBAs[m_nTypes].pHeader = NULL;
		}
	}
} 

void CPacket_exit()
{   
    LOG("free m_pOriginBuffer,m_PBAs\n");
    
	if(m_pOriginBuffer){
		free(m_pOriginBuffer);
	}
    
	for(; m_nTypes; m_nTypes--){
		free(m_PBAs[m_nTypes-1].pArray);
	}
}

void CPacket_test(int argc, char **argv)
{
    if(argc == 1)
        m_nPages = atoi(argv[0]);
    
	CPacket_init();
	DumpNodes();
    
    CPacket *pPacket[12] = {0};
    int i, count = sizeof(pPacket)/sizeof(pPacket[0]);
	unsigned long size = 0;
	unsigned long page = 0;
	unsigned long len;    
    
    srand((unsigned)time(NULL));    
    
    for(i = 0; i < count; i++){
        len = ((rand() % PKT_PAGE_SIZE) + 1);
        pPacket[i] = GetPacket(len);
		if(!pPacket[i]){
			break;
		}
		size += len;
		page += (len + PKT_PAGE_SIZE - 1) / PKT_PAGE_SIZE;
    }

    DumpUsage();
	LOG("Test() rand allocate usage: in byte %d%%, in page %d%%.\n", 
        100*size/PKT_PAGE_SIZE/m_nPages, 
        100*page/m_nPages);    
    
	DumpNodes();
    
	for(i = 0; i < count; i++){
		if(pPacket[i]){
			PutPacket(pPacket[i]);
		}
	}
    
    CPacket_exit();
}


