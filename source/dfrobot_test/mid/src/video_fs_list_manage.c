#include "common.h"
#include "video_fs_manage.h"
#include "video_fs_list_manage.h"


static void *record_replay_queue = NULL;

  

//����¼��Ӧ�ö�Ӧ��¼���б��ļ�,���ط�ʹ��
static char g_video_list_file_name[RECORD_APP_NUM][32]=
{
	{"/tmp/record.list"},
	{"/tmp/alarm.list"}
};

static void video_fs_list_free_file_queue(void *item)
{

    if(item)
    {
        free(item);
    }
}


static Precord_replay_info video_fs_list_malloc_file_queue(int file_len)
{    
    return (Precord_replay_info )malloc(sizeof(record_replay_info));
}

/**
 * NAME         video_fs_list_compare_record_name
 * @BRIEF	�Ƚ�������¼�ļ�����С
  
 * @PARAM    item1
                    item2
 * @RETURN	�ȽϽ��
 * @RETVAL	
                   <0 item1<item2,
                   =0 item1=item2
                   >0 item1>item2
                   �����itemָ����item����ָ����ļ���
 */

static int video_fs_list_compare_record_name(void *item1, void *item2)
{
    Precord_replay_info precord1 = (Precord_replay_info)item1;
    Precord_replay_info precord2 = (Precord_replay_info)item2;

    return strcmp(precord1->file_name, precord2->file_name);
    
}

/**
 * NAME         video_fs_list_get_info
 * @BRIEF	�õ���Ӧ�ļ���¼��ʱ��
  
 * @PARAM    file_name  
                   type          Ӧ������(�ƻ�¼�񡢸澯¼���)
 * @RETURN	�ļ���¼�������Ϣ
 * @RETVAL	
 */

static Precord_replay_info video_fs_list_get_info(char *file_name , int type)
{
    int file_len;   
    Precord_replay_info pnew_record;
	struct tm time_st;
	char  s_time[6][10];
	const video_file_name_info * cfg;
	char file_name_fmt[100];
	int i,namepos;
	int record_time;
	
    file_len = strlen(file_name);
    record_time = video_demux_get_total_time(file_name);
    if (record_time <=0)
    	return NULL;
    pnew_record = video_fs_list_malloc_file_queue(file_len + 4);
    pnew_record->record_time =record_time;

	//������ļ�����ȡ��Ƶ��ʼʱ��
	cfg = video_fs_get_file_name_cfg();
	//����ȡʱ��ĸ�ʽ���� for CYC_DV_20150908-090511.mp4
	sprintf(file_name_fmt, "%s%%4c%%2c%%2c-%%2c%%2c%%2c%s", cfg[type].prefix,cfg[type].posfix);
	memset(s_time, 0, sizeof(s_time));	
	//ȥ���ļ���·��
	for(i=0 ;i< file_len;i++)
	{
		if (file_name[file_len-1-i] =='/')
			break;
	}
	namepos = file_len -i;
	sscanf(file_name+namepos, file_name_fmt,
		s_time[0],s_time[1],s_time[2],s_time[3],s_time[4],s_time[5]);
	time_st.tm_year = atoi(s_time[0]) -1900;
	time_st.tm_mon  = atoi(s_time[1]) -1;
	time_st.tm_mday = atoi(s_time[2]);
	time_st.tm_hour = atoi(s_time[3]);
	time_st.tm_min  = atoi(s_time[4]);
	time_st.tm_sec  = atoi(s_time[5]);
	time_st.tm_isdst= 8;

	pnew_record->recrod_start_time = mktime(&time_st);
	
	
	
    strcpy(pnew_record->file_name, file_name);
    pnew_record->record_time = pnew_record->record_time / 1000;
    
    return pnew_record;
}

/**
 * NAME         video_fs_list_insert_item
 * @BRIEF	��ָ�����ļ����뵽����
  
 * @PARAM    file_name   �ļ���
                    file_size    �ļ���С
                    type        Ӧ������(�ƻ�¼�񡢸澯¼���)
 * @RETURN	NONE
 * @RETVAL	
 */

static void video_fs_list_insert_item(char *file_name, int file_size,int type)
{
    Precord_replay_info pnew_record;
    pnew_record = video_fs_list_get_info(file_name,type);

    if (pnew_record ==NULL)
    	return ;
    if(pnew_record->recrod_start_time < 1418954000) //ֻȡ2014-12-19 ����ļ�
    {
        video_fs_list_free_file_queue(pnew_record);
        return ;
    }
    
    if(0 == anyka_queue_push(record_replay_queue, (void *)pnew_record))
    {
        video_fs_list_free_file_queue(pnew_record);
    }
}


/**
 * NAME         video_fs_list_init_list
 * @BRIEF	ͳ��ָ��Ŀ¼�µ�¼���ļ�������ס����������ʱ�ļ���
  
 * @PARAM    in_dir         Ŀ¼��Ϣ         
                    save_file    �����ļ���
                    type: 0-->video record, 1-->alarm record
 * @RETURN	NONE
 * @RETVAL	
 */
static void video_fs_list_init_list(char * in_dir, char *save_file, unsigned short type)
{
    char *file_buf;
    int file_record_list;
    Precord_replay_info pcur_record;
	
    file_record_list = open(save_file,  O_RDWR | O_CREAT | O_TRUNC,S_IRUSR|S_IWUSR );
    if(file_record_list < 0)
    {
        return ;
    }
    file_buf = (char *)malloc(1024);
    record_replay_queue = anyka_queue_init(1024*10);
	if(record_replay_queue == NULL)
	{
		anyka_print("[%s:%d] Fails to init the queue:%s\n", __func__, __LINE__, "record_replay_queue");
	}

	video_fs_init_record_list(in_dir, video_fs_list_insert_item, type);    
	
    anyka_queue_sort(record_replay_queue, video_fs_list_compare_record_name);

    while(anyka_queue_not_empty(record_replay_queue))
    {
        pcur_record = anyka_queue_pop(record_replay_queue);
        if(pcur_record)
        {
            sprintf(file_buf, "file:%s;start:%ld;time:%ld\n", pcur_record->file_name, pcur_record->recrod_start_time, pcur_record->record_time);
            write(file_record_list, file_buf, strlen(file_buf));
            video_fs_list_free_file_queue(pcur_record);
        }
    }
    anyka_queue_destroy(record_replay_queue, video_fs_list_free_file_queue);
    free(file_buf);
    close(file_record_list);
    record_replay_queue = NULL;
}

/**
 * NAME         video_fs_list_init
 * @BRIEF	Ӧ�ó�������ʱ������¼���ļ��б�
  
 * @PARAM    user                
 * @RETURN	NONE
 * @RETVAL	
 */

//void record_replay_test();
void video_fs_list_init(  )
{
	video_setting * pvideo_record_setting = anyka_get_sys_video_setting();    
    Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();

    video_fs_list_init_list(pvideo_record_setting->recpath, (char *)g_video_list_file_name[RECORD_APP_PLAN], RECORD_APP_PLAN); //video
    video_fs_list_init_list(alarm_info->alarm_default_record_dir, (char *)g_video_list_file_name[RECORD_APP_ALARM], RECORD_APP_ALARM);	//alarm
    /***************************************************
       *****************************************************/
}

/**
 * NAME         video_fs_list_insert_file
 * @BRIEF	����һ�����͵�¼���ļ���һ���ڼƻ�¼��򱨾�¼�����
  
 * @PARAM    type                �طŵ�����
                    file_name        ¼���ļ���
 * @RETURN	NONE
 * @RETVAL	
 */

void video_fs_list_insert_file(char *file_name, int type)
{
    char *file_buf;
    int record_list;
    Precord_replay_info pcur_record;
    char *save_file;
    
//    anyka_print("[%s,%d]it inserts record list(name:%s; type:%d)!\n", __FUNCTION__, __LINE__, file_name, type);
    save_file = (char *)&g_video_list_file_name[type];
    
    record_list = open(save_file,  O_RDWR | O_APPEND);
    if(record_list < 0)
    {
        anyka_print("[%s,%d]it fails to open record list(%s)!\n", __FUNCTION__, __LINE__, save_file);
        return ;
    }
    pcur_record = video_fs_list_get_info(file_name,type);
    if(pcur_record)
    {
	    if(pcur_record->recrod_start_time >= 1418954000) //ֻȡ2014-12-19 ����ļ�
	    {
        file_buf = (char *)malloc(1024);
        
        sprintf(file_buf, "file:%s;start:%ld;time:%ld\n", pcur_record->file_name, pcur_record->recrod_start_time, pcur_record->record_time);
        write(record_list, file_buf, strlen(file_buf));
        
        free(file_buf);
      }
        video_fs_list_free_file_queue(pcur_record);
    }
    close(record_list);
}

/**
 * NAME         video_fs_list_remove_file
 * @BRIEF	ɾ��һ�����͵�¼���ļ���һ���ڼƻ�¼��򱨾�¼�����
  
 * @PARAM    type                �طŵ�����
                    file_name        ¼���ļ���
 * @RETURN	NONE
 * @RETVAL	
 */

void video_fs_list_remove_file(char *file_name, int type)
{
    char *file_buf;
    FILE * record_list, *new_record;
    char *save_file;
    int file_len = strlen(file_name);

    save_file = (char *)&g_video_list_file_name[type];
    
    record_list = fopen(save_file,  "r");
    if(record_list == NULL)
    {
        return ;
    }
    new_record = fopen("/tmp/tmp.txt",  "w");
    if(new_record == NULL)
    {
        fclose(record_list);
        return ;
    }
    file_buf = (char *)malloc(1024);
    
    while(!feof(record_list))
    {
        if(fgets(file_buf, 1024, record_list) == NULL)
        {
            break;
        }
        if(strstr(file_buf,"file:"))
        {
            if(memcmp(file_buf + 5, file_name, file_len) == 0)
            {
                continue;
            }
        }
        fputs(file_buf, new_record);
    }        
    
    fclose(record_list);
    fclose(new_record);
    remove(save_file);
    rename("/tmp/tmp.txt", save_file);
    free(file_buf);
}


static void video_fs_list_format_info(char *buf, char *file_name, unsigned long *start, unsigned long *cnt)
{
    char *find;

    find = strstr(buf, "file:");
    if(find)
    {
        find += 5;
        while(*find && *find != ';')
        {
            *file_name = *find;
            file_name ++;
            find ++;
        }
        *file_name = 0;
    }
    find = strstr(buf, "start:");
    if(find)
    {
        find += 6;
        *start = atoi(find);
    }
    find = strstr(buf, "time:");
    if(find)
    {
        find += 5;
        *cnt = atoi(find);
    }
}

/**
 * NAME         video_fs_list_load_list
 * @BRIEF	 ����ָ�����͵����м�¼�������ļ�����������
  
 * @PARAM    type                �طŵ�����
 * @RETURN	���ط��ϻطŵļ�¼ָ��
 * @RETVAL	
 */

static Precord_replay_info video_fs_list_load_list(int type)
{
    char *file_buf;
    FILE * record_list;
    Precord_replay_info cur, head = NULL, tail = NULL;
    char *save_file;
    unsigned long record_end_time;
    
    save_file = (char *)&g_video_list_file_name[type];

    record_list = fopen(save_file,  "r");
    if(record_list == NULL)
    {
        return NULL;
    }
    file_buf = (char *)malloc(1024);
    
    while(!feof(record_list))
    {
        if(fgets(file_buf, 1024, record_list) == NULL)
        {
            break;
        }
        if(strstr(file_buf,"file:"))
        {
        	//get one frame info store in cur
            cur = (Precord_replay_info)malloc(sizeof(record_replay_info));
            video_fs_list_format_info(file_buf, cur->file_name, &cur->recrod_start_time, &cur->record_time);
            cur->next = NULL;	//next frame
            if(head == NULL)
            {
                head = cur;
            }
            else
            {
            	//ǰ��Ƭ��ȥ��, ͨ������¼��ʱ��ʹ����ʱ����ǰ
            	record_end_time = tail->recrod_start_time + tail->record_time;
            	
            	if (cur->recrod_start_time <= record_end_time)
            	{
            		tail->record_time -= cur->recrod_start_time - record_end_time + 1;
            	}
                tail->next = cur;
            }
            tail = cur;
        }
    }        
    
    free(file_buf);
    fclose(record_list);
    return head;	//return the frame info
}

/**
 * NAME         video_fs_list_check_record
 * @BRIEF	 ��鵱ǰ��¼�Ƿ���ָ��ʱ����
  
 * @PARAM    start_time        �طŵ���ʼʱ��
                    end_time         �طŵĽ���ʱ��
                    cur                  ��ǰ���ļ�¼
 * @RETURN	1-->���������ڣ�0-->������������
 * @RETVAL	
 */

static int video_fs_list_check_record(unsigned long  start_time, unsigned long end_time, Precord_replay_info cur)
{
	/** �ֱ𱣴浱ǰ¼���ļ�����ʼʱ��ͽ���ʱ�� **/
    int record_start, record_end;

    record_start = cur->recrod_start_time;
    record_end = cur->recrod_start_time + cur->record_time;

	/** ���������� **/
    if(record_start >= end_time ||  record_end <= start_time)
    {
        return 0;
    }/** �������ڵ� **/
    else if(record_start < start_time)
    {
        cur->play_start = start_time - record_start;
        cur->play_time = record_end - start_time;
    }
    else if(record_start >= start_time && record_end <= end_time)
    {
        cur->play_start = 0;
        cur->play_time = cur->record_time;
    }
    else if(record_start < end_time && record_end > end_time)
    {
        cur->play_start = 0;
        cur->play_time = end_time - record_start;
    }
	
    return 1;
}

/**
 * NAME         video_fs_list_get_record
 * @BRIEF	 ͳ�Ʒ��ϻطŵļ�¼��Ϣ
  
 * @PARAM    start_time        �طŵ���ʼʱ��
                    end_time         �طŵĽ���ʱ��
                    type                �طŵ�����
 * @RETURN	���ط��ϻطŵļ�¼ָ��
 * @RETVAL	
 */

Precord_replay_info video_fs_list_get_record(unsigned long  start_time, unsigned long end_time, int clip_limit , int type)
{
    Precord_replay_info head = NULL, cur, tail = NULL;
    Precord_replay_info all_list;
	int count = 0;

	/** ��ȡ����¼���ļ���Ϣ **/
	all_list = video_fs_list_load_list(type);	//allocate mem no release
	

    while(all_list)
    {
        cur = all_list;
        all_list = all_list->next;
        if(video_fs_list_check_record(start_time, end_time, cur))
        {
        	if (clip_limit > 0) //��Ҫ����Ƭ�θ���
        	{
	        	if (count >= clip_limit)
	        	{
		            free(cur);
    	    		continue;
    	    	}
    	    }
        		
            cur->next = NULL;
            if(head == NULL)
            {
                head = cur; 
            }
            else
            {
                tail->next = cur;
            }
            tail = cur;
			count++;
        }
        else
        {
            free(cur);
        }
    }

    return head;
}


#if 0
/**
* @brief  video_fs_cmp_alarm_record_file
* 			�Ƚ��ƶ����¼���ļ�
* @date 	2015/3
* @param:	char * file_name�� ���Ƚϵ��ļ���
* @return 	uint8 
* @retval 	�ǵ�ǰϵͳ¼�Ƶ��ļ�-->1, ����-->0
*/

uint8 video_fs_cmp_alarm_record_file(char *file_name)
{
	int file_len, cmp_len, record_file_len;

	cmp_len = strlen(RECORD_SAVE_SUF_NAME);
    file_len = strlen(file_name);
    record_file_len = 6 + strlen(OUR_VIDEO_ALARM_FILE_PREFIX) + cmp_len;
    if(file_len < record_file_len)
    {
        anyka_print("[%s:%d] %s isn't anyka video record!, (%d,%d)\n",
			   		 __func__, __LINE__, file_name, file_len, record_file_len);
        return 0;
    }

	/**** first compare prefix ****/
    if(memcmp(file_name, OUR_VIDEO_ALARM_FILE_PREFIX, strlen(OUR_VIDEO_ALARM_FILE_PREFIX)))
    {
        anyka_print("[%s:%d] %s isn't anyka video record!, %s\n",
			   		__func__, __LINE__, file_name, OUR_VIDEO_ALARM_FILE_PREFIX);
        return 0;
    }

	/**** then compare suffix ****/
    if(memcmp(file_name + file_len - cmp_len, RECORD_SAVE_SUF_NAME, cmp_len))
    {
        anyka_print("[%s:%d] %s isn't anyka video record!, %s\n", 
					__func__, __LINE__, file_name, RECORD_SAVE_SUF_NAME);
        return 0;
    }
    return 1;
}



/**
* @brief  video_fs_cmp_video_record_file
* 			�Ƚϼƻ�¼���ļ�
* @date 	2015/3
* @param:	char * file_name�� ���Ƚϵ��ļ���
* @return 	uint8 
* @retval 	�ǵ�ǰϵͳ¼�Ƶ��ļ�-->1, ����-->0
*/

uint8 video_fs_cmp_video_record_file(char *file_name)
{
	int file_len, cmp_len, record_file_len;

	cmp_len = strlen(RECORD_SAVE_SUF_NAME);
    file_len = strlen(file_name);
    record_file_len = 6 + strlen(VIDEO_FILE_PREFIX) + cmp_len;
    if(file_len < record_file_len)
    {
        anyka_print("[%s:%d] %s isn't anyka video record!, (%d,%d)\n",
			   		 __func__, __LINE__, file_name, file_len, record_file_len);
        return 0;
    }
	
	/**** first compare prefix ****/
    if(memcmp(file_name, VIDEO_FILE_PREFIX, strlen(VIDEO_FILE_PREFIX)))
    {
        anyka_print("[%s:%d] %s isn't anyka video record!, %s\n",
			   		__func__, __LINE__, file_name, VIDEO_FILE_PREFIX);
        return 0;
    }

	/**** then compare suffix ****/
    if(memcmp(file_name + file_len - cmp_len, RECORD_SAVE_SUF_NAME, cmp_len))
    {
        anyka_print("[%s:%d] %s isn't anyka video record!, %s\n", 
					__func__, __LINE__, file_name, RECORD_SAVE_SUF_NAME);
        return 0;
    }
    return 1;
	
}

#endif

#if 0
/**
* @brief  video_fs_check_record_dir_name
* 			����Ŀ¼�������ͱȽϵ�ǰĿ¼�Ƿ�Ϊ���¼��Ŀ¼
* @date 	2015/3
* @param:	char * dir_name�� ���Ƚϵ�Ŀ¼
			unsigned short type��RECORD_REPLAY_TYPE �ƻ�¼�����ͣ�����Ϊ�ƶ��������
* @return 	uint8 
* @retval 	�ǵ�ǰϵͳ¼�Ƶ��ļ�-->1, ����-->0
*/

uint8 video_fs_check_record_dir_name(char * dir_name, unsigned short type)
{
	int ret = 0;
	if(type == RECORD_REPLAY_TYPE)	//video
	{
		if(0 == memcmp(dir_name, VIDEO_FILE_PREFIX, strlen(VIDEO_FILE_PREFIX)))
			ret = 1;
	}
	
	else if(type == ALARM_REPLAY_TYPE)	//alarm
	{
		if(0 == memcmp(dir_name, ALARM_DIR_PREFIX, strlen(ALARM_DIR_PREFIX)))
			ret = 1;
	}

	return ret;
}
#endif 

