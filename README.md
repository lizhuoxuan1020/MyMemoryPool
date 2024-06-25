
# �ڴ��
����Ŀּ��ʵ��һ���߲������ڴ�أ��������Դ��Google��Դ��Ŀtcmalloc��tcmalloc��ȫ��ΪThread-Caching Malloc�����̻߳����malloc������Ч�ع����˶��߳��µ��ڴ���䣬���������ϵͳ�ṩ��malloc��free����������Ŀ����tcmalloc�ĺ��Ŀ�ܣ����������ȵأ��򻯺�ģ��ʵ�֣�ʵ����һ���Լ���mini���ڴ�ء�  
## ��Ŀ����
���չȸ�tcmalloc�߲����ڴ�صļܹ���ÿ���̶߳��ж������̻߳���ThreadCache���̵߳��ڴ�����������ThreadCache���룬ThreadCache�ڴ治���û������뻺��CentralCache���룬CentralCache�ڴ治����ʱ����ҳ��PageHeap���룬PageHeap�����þͻ���OS����ϵͳ���롣    
![�ڴ��](images/memory_pool.png)  


#### �̻߳��� ThreadCache
ThreadCache��ÿ���̶߳���ӵ�е�Cache��������������ڴ�����size classes����ÿһ������size-class�����д�С��ͬ��object��  
�߳̿��ԴӸ���Thread Cache��FreeList��ȡ���󣬲���Ҫ�����������ٶȺܿ졣���ThreadCache��FreeListΪ�գ���Ҫ��CentralCache�е�CentralFreeList�л�ȡ���ɸ�object��ThreadCache��Ӧ��size class�б��У�Ȼ����ȡ������һ��object���ء� 
![ThreadCache](images/thread_cache.png)  

�ڴ��object
ThreadCache�ж���ܶ����ͬ�ߴ���ڴ�飨��Ϊobject�������ͬ�ߴ�����ڴ�����ӳ�һ���ɷ����FreeList. ����google��tcmalloc�г�Ϊsize class��
������С�ڴ�ʱ(С��256K)���ڴ�ػ���������ڴ��Сӳ�䵽ĳ��freelist�С����磬����0��8���ֽڵĴ�Сʱ���ᱻӳ�䵽freelists[0]�У�����8���ֽڴ�С������9��16�ֽڴ�Сʱ���ᱻӳ�䵽freelists[1]�У�����16���ֽڴ�С���Դ����ơ�

#### ���뻺�� CentralCache
���뻺��CentralCache��ThreadCache�Ļ��棬ThreadCache�ڴ治��ʱ����CentralCache���롣CentralCache������һ��CentralFreeList������������ThreadCache������ͬ��ThreadCache���ڴ����ʱ�����ԷŻ�CentralCache�С�  
���CentralFreeList�е�object������CentralFreeList����PageHeap����һ������Span��ɵ�Page�����������Page�и��һϵ�е�object���ٽ�����objectת�Ƹ�ThreadCache��
��������ڴ����256Kʱ������ͨ��ThreadCache���䣬����ͨ��PageHeapֱ�ӷ�����ڴ档 
![CentralCache](images/central_cache.png)  



#### ҳ�� PageHeap
ҳ��PageHeap����洢Span����������CentralCache�ڴ治��ʱ�����Դ�PageHeap��ȡSpan��Ȼ���Span�и��object��  
PageHeap�����ڴ�ʱ����Page���룬�������ڴ�Ļ�����λ��Span��Span������������Page��  

PageHeap��֯�ṹ���£�
![PageHeap](images/page_heap.png)  



Span��PageHeap�й����ڴ�Page�ĵ�λ����һ������������Page��ɣ�����2��Page��ɵ�span�����spanʹ�������������ڴ����SpanΪ��λ�����ϵͳ�����ڴ档
��1��span����2��page����2���͵�4��span����3��page����3��span����5��page��
Span���¼��ʼpage��PageID��start���Լ�������page��������length����  

![Span](images/span.png)    

![SpanLists](images/spanlists.png)    

span�а�������Span���͵�ָ�루prev��next�������ڽ����span���������ʽ�洢��
��google tcmalloc�е�Span������״̬IN_USE, ON_NORMAL_FREELIST, ON_RETURNED_FREELIST. ����Ŀ����򻯣���һ���������usecount����ʶspan��ǰ�ж��ٸ��ڴ�鱻���䡣��  
  



## ��Ŀ�ο�
[�����ڴ��](https://www.geeksforgeeks.org/what-is-a-memory-pool/)  
[google��tcmalloc��Դ��Ŀ](https://github.com/google/tcmalloc/tree/master)  
[tcmallocѧϰ](https://blog.csdn.net/qq_40989769/article/details/136540173)  
[tcmallocѧϰ2](https://blog.csdn.net/weixin_45266730/article/details/131421670)  
[STL��׼��Ŀռ�������allocatorԴ��](https://gcc.gnu.org/onlinedocs/gcc-4.6.3/libstdc++/api/a00751_source.html)    
[�����ڴ�ؿ�ĶԱ�](https://blog.csdn.net/junlon2006/article/details/77854898)  

##  
