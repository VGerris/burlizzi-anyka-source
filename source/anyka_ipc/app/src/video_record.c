/*******************************************************************
���ļ���ɵļƻ�¼�����ع��ܡ�
Ŀǰ��ƣ���һ���߳�ר��������������ر��ļ������¼���ļ��ĺϳ�
����һ���߳����ڱ���������ݣ����ⷽʽ����֤¼�����ݲ���ʧ��
�����¼������г����쳣�ο��Ķ��������ܻᵼ���ļ�����û��
д��SD�����յ��¿���ֻ�����߶�ʧ�ռ䡣
����ѭ��¼��SD¼���󣬽������ļ���(��ʱ������)���Ⱥ�˳��ɾ�������
�����ͨ¼��¼���󣬽��Զ��˳�¼��
*******************************************************************/


#include "common.h"
#include "video_fs_manage.h"
#include "anyka_ftp_update.h"
#ifdef CONFIG_DANA_SUPPORT
#include "dana_interface.h"
#endif
#ifdef CONFIG_TENCENT_SUPPORT

#include "tencent_init.h"
int ak_qq_check_video_filter(void);
#endif



#define RECORD_SAVE_PATH                "/mnt"
#define RECORD_TMP_PATH                "/mnt/tmp/"
#define MIN_DISK_SIZE_FOR_RECORD		(200*1024)  //���̵ı����ռ䣬��λ��KB
#define MAX_RECORD_FILE_NAME_LEN        100         //����¼���ļ���
#define RECORD_DEFAULT_TIME             (5*60*1000) //����¼���ļ���ʱ��

/**********************************************************************
����¼��ʱ��Ĭ�ϴ��������ļ���anyka_video_record_1��anyka_video_record_2
***********************************************************************/

const char *anyka_video_tmp_record_file[] =
{
    "/mnt/tmp/anyka_video_record_1",
    "/mnt/tmp/anyka_video_record_2",
};

/**********************************************************************
����¼��ʱ��Ĭ�ϴ������������ļ���anyka_video_index_1��anyka_video_index_2
***********************************************************************/

const char *anyka_video_tmp_index_file[] =
{
    "/mnt/tmp/anyka_video_index_1",
    "/mnt/tmp/anyka_video_index_2",
};
typedef struct _video_record_file
{
    int record_file;
    int index_file;
}video_record_file;

typedef struct _video_record_info
{
    uint32  file_size;      //¼���ļ��Ĵ�С
    char    *file_name;     //¼���ļ���
}video_record_info;


typedef struct _video_record_handle
{
    uint8   run_flag;   //������رձ�־
    uint8   cyc_flag;   //�Ƿ�ѭ��¼��
    uint8   stop_flag;  //¼��д�ļ�ʧ�ܱ�־
    uint8   use_file_index; //��ǰ����ʹ�õ��ļ�����
    uint8   rec_save_index; //������ļ�����
    void    *record_file_queue;  //¼��������ݵĶ��С�
    video_record_file record_file[2];
    char    record_file_name[2][MAX_RECORD_FILE_NAME_LEN];
    char    record_dir_name[MAX_RECORD_FILE_NAME_LEN];
    uint32  record_total_size;  //����¼��Ŀռ��С������δ�ÿռ���Ѿ�¼��Ŀռ�
    uint32  used_record_file_size;   //¼���Ѿ�ʹ�õĶ��ٿռ�
    uint32  record_time;             //¼���ļ������ʱ��
    uint32  record_end_time;        //��ǰ¼���ļ��Ľ���ʱ��
    uint32  video_start_time;
    void    *mux_handle;
    void    *save_mux_handle;
    sem_t   sem_create_file;
    pthread_mutex_t     video_record_mutex;
    PA_HTHREAD  create_record_handle_id;
}video_record_handle, *Pvideo_record_handle;



static Pvideo_record_handle pvideo_record_ctrl = NULL;


void video_record_free_file_queue(void *item)
{
    video_record_info *file_info = (video_record_info*)item;

    if(file_info)
    {
        free(file_info->file_name);
        free(file_info);
    }
}


video_record_info *video_record_malloc_file_queue(int file_len)
{    
    video_record_info *file_info = NULL;

    file_info = (video_record_info *)malloc(sizeof(video_record_info));
    if(file_info)
    {
        file_info->file_name = (char *)malloc(file_len);
        if(file_info->file_name == NULL)
        {
            free(file_info);
            return NULL;
        }
    }
    return  file_info;
}

/**
 * NAME         video_record_flush_record_file
 * @BRIEF	������ʱ �ļ�
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

void video_record_flush_record_file(int index)
{
    if(pvideo_record_ctrl->record_file[index].index_file > 0)
    {
        close(pvideo_record_ctrl->record_file[index].index_file);
        remove(anyka_video_tmp_index_file[index]);    
    }

    if(pvideo_record_ctrl->record_file[index].record_file > 0)
    {
        fsync(pvideo_record_ctrl->record_file[index].record_file);
        close(pvideo_record_ctrl->record_file[index].record_file);
        anyka_print("[%s:%d] the record name is %s\n", __func__, __LINE__, pvideo_record_ctrl->record_file_name[index]);
        rename(anyka_video_tmp_record_file[index], pvideo_record_ctrl->record_file_name[index]);
        video_fs_list_insert_file(pvideo_record_ctrl->record_file_name[index], RECORD_APP_PLAN);
    }
    pvideo_record_ctrl->record_file[index].index_file = 0;
    pvideo_record_ctrl->record_file[index].record_file = 0;
}

/**
 * NAME         video_record_del_record_file
 * @BRIEF	ɾ����ʱ �ļ�
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

void video_record_del_record_file(int index)
{
    if(pvideo_record_ctrl->record_file[index].index_file > 0)
    {
        close(pvideo_record_ctrl->record_file[index].index_file);
        remove(anyka_video_tmp_index_file[index]);    
    }
    
    if(pvideo_record_ctrl->record_file[index].record_file > 0)
    {
        close(pvideo_record_ctrl->record_file[index].record_file);
        remove(anyka_video_tmp_record_file[index]);    
    }
    pvideo_record_ctrl->record_file[index].index_file = 0;
    pvideo_record_ctrl->record_file[index].record_file = 0;
}


/**
 * NAME         video_record_create_record_file
 * @BRIEF	������ʱ �ļ���
 * @PARAM	index
 * @RETURN	0-->fail; 1-->ok
 * @RETVAL	
 */

uint8 video_record_create_record_file(int index)
{
	char res[1000] = {0};
	
    pvideo_record_ctrl->record_file[index].record_file = open(anyka_video_tmp_record_file[index], O_RDWR | O_CREAT | O_TRUNC,S_IRUSR|S_IWUSR);
    if(pvideo_record_ctrl->record_file[index].record_file <= 0)
    {
		/** read-only file system **/
		if(EROFS == errno)
		{
			anyka_print("[%s:%d] %s, repair it ...\n", __func__, __LINE__, strerror(errno));

			do_syscmd("mount | grep '/dev/mmcblk0p1 on /mnt'", res);
			if(strlen(res) > 0) {
				anyka_print("[%s:%d] remount /dev/mmcblk0p1\n", __func__, __LINE__);
				system("mount -o remount,rw /dev/mmcblk0p1");
				return 0;
			}

			bzero(res, 1000);

			do_syscmd("mount | grep '/dev/mmcblk0 on /mnt'", res);
			if(strlen(res) > 0) {
				anyka_print("[%s:%d] remount /dev/mmcblk0\n", __func__, __LINE__);
				system("mount -o remount,rw /dev/mmcblk0");
				return 0;
			}
			
		} 
		else
			anyka_print("[%s:%d] %s\n", __func__, __LINE__, strerror(errno));
        return 0;
    }
	
    pvideo_record_ctrl->record_file[index].index_file = open(anyka_video_tmp_index_file[index], O_RDWR | O_CREAT | O_TRUNC,S_IRUSR|S_IWUSR);
    if(pvideo_record_ctrl->record_file[index].index_file <= 0)
    {
        close(pvideo_record_ctrl->record_file[index].record_file);
        pvideo_record_ctrl->record_file[index].record_file = 0;
        remove(anyka_video_tmp_record_file[index]); 		
        anyka_print("[%s:%d] %s\n", __func__, __LINE__, strerror(errno));
        return 0;
    }
    return 1; 
}


/**
 * NAME         video_record_get_record_file_name
 * @BRIEF	�õ�¼���ļ�������ʱ��Ϊ����
 * @PARAM	index
                   file_time  �ļ�����ʱ��
 * @RETURN	void
 * @RETVAL	
 */
void video_record_get_record_file_name(int index,time_t file_time)
{
    video_record_info *new_note;

	//�����ļ��������غ�record_dir_name ��ŵ���¼���ļ�����·���ļ���
    video_fs_get_video_record_name(pvideo_record_ctrl->record_dir_name, pvideo_record_ctrl->record_file_name[index], ".mp4",file_time);
    new_note = video_record_malloc_file_queue(strlen(pvideo_record_ctrl->record_file_name[index]) + 4);

    strcpy(new_note->file_name , pvideo_record_ctrl->record_file_name[index]);
    new_note->file_size =0 ;//��ʼ�ļ��ߴ磬�ļ�¼�����ʱ����
    if(0 == anyka_queue_push(pvideo_record_ctrl->record_file_queue, (void *)new_note))
    {
        video_record_free_file_queue(new_note);
    }
}

/**
 * NAME         video_record_update_use_size
 * @BRIEF	������¼���ʹ���˿ռ��Ҫ�������¼���С
 * @PARAM	int file_size �����ļ���С
 * @RETURN	void*
 * @RETVAL	
 */


void video_record_update_use_size(int file_size)
{
    if(pvideo_record_ctrl)
    {
        pthread_mutex_lock(&pvideo_record_ctrl->video_record_mutex);
        pvideo_record_ctrl->used_record_file_size += file_size;
        pthread_mutex_unlock(&pvideo_record_ctrl->video_record_mutex);
    }
    
}

/**
 * NAME         video_record_manage_file
 * @BRIEF	�����ļ����߳�
 * @PARAM	void
 * @RETURN	void*
 * @RETVAL	
 */

void* video_record_manage_file(void *arg)
{   
    video_record_info *cyc_remove_file;
	
	anyka_print("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());
    
    while(pvideo_record_ctrl->run_flag)
    {
        sem_wait(&pvideo_record_ctrl->sem_create_file);
        if(pvideo_record_ctrl->run_flag)
        {
            if(pvideo_record_ctrl->save_mux_handle)
            {               
                uint32 cur_file_size;
                struct stat     statbuf; 
                int queue_amount ; 
                
                video_record_info *precord_info;
                //�ȹر�ǰһ���ϳɾ����
                mux_close(pvideo_record_ctrl->save_mux_handle);
                pvideo_record_ctrl->save_mux_handle = NULL;
                stat(anyka_video_tmp_record_file[pvideo_record_ctrl->rec_save_index], &statbuf);
                //��������ļ��Ĵ�С ��������
                cur_file_size = (statbuf.st_size >> 10) + ((statbuf.st_size & 1023) ? 1 : 0);//�����ļ���Сϵͳ������.
				//��һ���ļ��Ѿ�����У�¼���ļ��ǵ����ڶ���
				queue_amount = anyka_queue_items(pvideo_record_ctrl->record_file_queue);
				precord_info = anyka_queue_get_index_item(pvideo_record_ctrl->record_file_queue, queue_amount-2);
//                precord_info = anyka_queue_get_tail_note(pvideo_record_ctrl->record_file_queue);
                if(precord_info)
                    precord_info->file_size = cur_file_size;

                pthread_mutex_lock(&pvideo_record_ctrl->video_record_mutex);
                pvideo_record_ctrl->used_record_file_size += cur_file_size;
                pthread_mutex_unlock(&pvideo_record_ctrl->video_record_mutex);
                //pvideo_record_ctrl->record_total_size -= cur_file_size;
                //����ռ����ˣ��ֲ���ѭ��¼�񣬽�ֹͣ¼��
                if((pvideo_record_ctrl->cyc_flag == 0) && (pvideo_record_ctrl->used_record_file_size >= pvideo_record_ctrl->record_total_size))
                {
                    pvideo_record_ctrl->stop_flag = 1;
                }
            }
          
            //�˴���ȡ��һ��¼��ʼ�ļ���
//            video_record_get_record_file_name(pvideo_record_ctrl->use_file_index);

            /*************************************************************
                    		�ȹر�ǰһ���ļ����ٴ���һ���ļ�
                    	**************************************************************/
			video_record_flush_record_file(pvideo_record_ctrl->rec_save_index);
            video_record_create_record_file(pvideo_record_ctrl->rec_save_index);
       
            //�����ѭ��¼��Ļ����ռ��Ѿ����󣬽�ɾ����һ���ļ�
            if(pvideo_record_ctrl->cyc_flag)
            {
               while(pvideo_record_ctrl->used_record_file_size >= pvideo_record_ctrl->record_total_size)
               {
                   cyc_remove_file = (video_record_info *)anyka_queue_pop(pvideo_record_ctrl->record_file_queue);
                   if(cyc_remove_file == NULL)
                   {
                        pvideo_record_ctrl->stop_flag = 1;
                        break;
                   }
                   remove(cyc_remove_file->file_name);   
                   video_fs_list_remove_file(cyc_remove_file->file_name, RECORD_APP_PLAN);
                   pthread_mutex_lock(&pvideo_record_ctrl->video_record_mutex);
                   pvideo_record_ctrl->used_record_file_size -= cyc_remove_file->file_size;           
                   pthread_mutex_unlock(&pvideo_record_ctrl->video_record_mutex);
                   anyka_print("[%s:%d] room isn't enough, remove %s(size:%d)\n", __func__, __LINE__, 
								 cyc_remove_file->file_name, pvideo_record_ctrl->used_record_file_size);
                   video_record_free_file_queue(cyc_remove_file);
               }
            }
        }
    }

	anyka_print("[%s:%d] This thread is going to exit(id: %ld)\n", __func__, __LINE__, (long int)gettid());

	return NULL;
}

/**
 * NAME         video_record_get_video_data
 * @BRIEF	��Ƶ���ݵĻص�
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */


void video_record_get_video_data(void *param, T_STM_STRUCT *pstream)
{
    uint8 ret = 1;

    if(pvideo_record_ctrl->run_flag == 0)
    {
        return;
    }

    if(pvideo_record_ctrl->record_end_time == 0)
    {
        //ɾ�����ǵ�һ��I֡��ͼ��
        if(pstream->iFrame == 0)
        {
            anyka_print("[%s:%d] we will pass the first P frmae!\n",  __func__, __LINE__);
            return;
        }
        pvideo_record_ctrl->record_end_time = pvideo_record_ctrl->record_time + pstream->timestamp;
        anyka_print("[%s:%d] first time: %lu, next time: %u, record_time: %u\n",  __func__, __LINE__,
				pstream->timestamp, pvideo_record_ctrl->record_end_time, pvideo_record_ctrl->record_time);
        video_record_get_record_file_name(pvideo_record_ctrl->use_file_index,pstream->calendar_time);
				
    }
    pthread_mutex_lock(&pvideo_record_ctrl->video_record_mutex);
    
    if(pvideo_record_ctrl->record_end_time  > pstream->timestamp + (pvideo_record_ctrl->record_time << 1)) 
    {
        pvideo_record_ctrl->record_end_time = pvideo_record_ctrl->record_time + pstream->timestamp;
    }
	
	//���¼��ʱ�䵽���������֡ΪI֡���ſ�ʼ������һ��¼��
    if((pvideo_record_ctrl->record_end_time <= pstream->timestamp) && pstream->iFrame)
    {
        pvideo_record_ctrl->save_mux_handle = pvideo_record_ctrl->mux_handle;
		pvideo_record_ctrl->rec_save_index = pvideo_record_ctrl->use_file_index;
        pvideo_record_ctrl->use_file_index = pvideo_record_ctrl->use_file_index ? 0 : 1;
		
		anyka_print("[%s:%d] video record using index: %d\n", 
					__func__, __LINE__, pvideo_record_ctrl->use_file_index);
		
        if(pvideo_record_ctrl->record_file[pvideo_record_ctrl->use_file_index].record_file == 0)
        {
            anyka_print("[%s:%d] the record file doesn't opened\n", __func__, __LINE__);
            ret = 0;
        }
        else
        {
            T_MUX_INPUT mux_input;
            
            mux_input.m_MediaRecType = MEDIALIB_REC_3GP;//MEDIALIB_REC_AVI_NORMAL;
            mux_input.m_eVideoType = MEDIALIB_VIDEO_H264;
            mux_input.m_nWidth = VIDEO_WIDTH_720P;
            mux_input.m_nHeight = VIDEO_HEIGHT_720P;
            
            //mux audio
            mux_input.m_bCaptureAudio = 1;
            mux_input.m_eAudioType = MEDIALIB_AUDIO_AMR;//MEDIALIB_AUDIO_AAC;
            mux_input.m_nSampleRate = 8000;
            pvideo_record_ctrl->video_start_time = pstream->timestamp;
            video_record_get_record_file_name(pvideo_record_ctrl->use_file_index,pstream->calendar_time);
            pvideo_record_ctrl->mux_handle = mux_open(&mux_input, pvideo_record_ctrl->record_file[pvideo_record_ctrl->use_file_index].record_file, 
                pvideo_record_ctrl->record_file[pvideo_record_ctrl->use_file_index].index_file);
            pvideo_record_ctrl->record_end_time += pvideo_record_ctrl->record_time;
            sem_post(&pvideo_record_ctrl->sem_create_file);
        }
    }
	
    mux_addVideo(pvideo_record_ctrl->mux_handle, pstream->buf, pstream->size, pstream->timestamp, pstream->iFrame);
    
    pthread_mutex_unlock(&pvideo_record_ctrl->video_record_mutex);
    if(ret == 0)
    {
        pvideo_record_ctrl->stop_flag = 1;
    }
}


/**
 * NAME         video_record_get_audio_data
 * @BRIEF	��Ƶ���ݵĻص�
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

void video_record_get_audio_data(T_VOID *param, T_STM_STRUCT *pstream)
{
    if(pvideo_record_ctrl->run_flag == 0)
    {
        return;
    }
    
    //ɾ�����е�����Ƶǰ�����Ƶ֡
    if(pvideo_record_ctrl->record_end_time == 0)
    {
        return ;
    }
    
    if(pstream->timestamp < pvideo_record_ctrl->video_start_time)
    {
        return ;
    }
    
    pthread_mutex_lock(&pvideo_record_ctrl->video_record_mutex);
    mux_addAudio(pvideo_record_ctrl->mux_handle, pstream->buf, pstream->size, pstream->timestamp);
    pthread_mutex_unlock(&pvideo_record_ctrl->video_record_mutex);
}


/**
 * NAME         video_record_insert_old_file
 * @BRIEF	��¼���ļ������в���һ���µ��ļ���
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

void video_record_insert_old_file(char *file_name, int file_size,int type)
{
    int file_len;    
    video_record_info * pnew_record;

    file_len = strlen(file_name);
    pnew_record = video_record_malloc_file_queue(file_len + 4);
    pnew_record->file_size = file_size;
    strcpy(pnew_record->file_name, file_name);
    if(0 == anyka_queue_push(pvideo_record_ctrl->record_file_queue, (void *)pnew_record))
    {
        video_record_free_file_queue(pnew_record);
    }
}



/**
 * NAME         video_record_compare_record_name
 * @BRIEF	�Ƚ�¼���ļ���
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

int video_record_compare_record_name(void *item1, void *item2)
{
    video_record_info *precord1 = (video_record_info *)item1;
    video_record_info * precord2 = (video_record_info *)item2;
    int len1, len2;

    len1 = strlen(precord1->file_name);
    len2 = strlen(precord2->file_name);

    return memcmp(precord1->file_name, precord2->file_name, len1>len2?len1:len2);

    
}

/**
 * NAME         video_record_check_run
 * @BRIEF	����Ƿ�����¼��
 * @PARAM	none 
 * @RETURN	0���ز���¼��1��������¼��
 * @RETVAL	
 */

int video_record_check_run(void)
{
    return pvideo_record_ctrl != NULL;
}


/**
* @brief  	video_record_check_dir_illegal
* 			���Ŀ¼�Ƿ���Ϲ淶: ��/ ��β˵
* @date 	2015/7
* @param:	const char * dir�� ������Ŀ¼
			
* @return 	int
* @retval 	���Ϲ淶����0�� ���򷵻�1
*/
static int video_record_check_dir_illegal(const char *dir)
{
	if(dir[strlen(dir) - 1] != '/')
		return 0;

	return 1;
}


/**
 * NAME         video_record_start
 * @BRIEF	¼������
 * @PARAM	void
 * @RETURN	-1����ʧ�ܣ�1���سɹ�
 * @RETVAL	
 */
int video_record_start(void)
{
    int ret;
	char record_dir[128] = {0};
	video_setting * pvideo_record_setting = anyka_get_sys_video_setting();
	video_record_setting *pvideo_record_msg =  anyka_get_record_plan();

	anyka_print("[%s:%d] #### Anyka_Record ###\n", __func__, __LINE__);


    //�ȼ��SD���Ƿ����
    if(check_sdcard() < 0)
    {
        anyka_print("[%s:%d] it fails to check sd\n", __func__, __LINE__);
        return -1;
    }

	if( !video_record_check_dir_illegal(pvideo_record_setting->recpath) )//Ŀ¼�����Ϲ�������/
	{
		sprintf(record_dir, "%s/", pvideo_record_setting->recpath);
        anyka_print("[%s:%d] the dir %s lost /, add it: %s\n", __func__, __LINE__, 
							pvideo_record_setting->recpath, record_dir);
	}
	else
		strcpy(record_dir, pvideo_record_setting->recpath);

	anyka_print("[%s:%d] dir: %s\n", __func__, __LINE__, record_dir);
    //����¼���ļ���
    video_fs_create_dir(record_dir);
	//������ʱ�ļ���
	video_fs_create_dir(RECORD_TMP_PATH); 
    pvideo_record_ctrl = (Pvideo_record_handle)malloc(sizeof(video_record_handle));
    if(pvideo_record_ctrl == NULL)
    {
        anyka_print("[%s:%d] it fails to malloc\n", __func__, __LINE__);
        return - 1;
    }
    memset(pvideo_record_ctrl , 0, sizeof(video_record_handle));
    pvideo_record_ctrl->use_file_index = 0;
	pvideo_record_ctrl->rec_save_index = 0;
    pvideo_record_ctrl->run_flag = 1;
    pvideo_record_ctrl->cyc_flag = pvideo_record_setting->save_cyc_flag;
    pvideo_record_ctrl->video_start_time = 0;

    //��ʼ��¼�����
    pvideo_record_ctrl->record_file_queue = anyka_queue_init(1500);
    if(pvideo_record_ctrl->record_file_queue == NULL)
    {
        goto VIDEO_RECORD_START_ERR;
    }
    //�����ѭ��¼���ܴ�С��Ҫ����֮ǰ¼����ļ�
	pvideo_record_ctrl->record_total_size = video_fs_get_free_size(record_dir);
    pvideo_record_ctrl->used_record_file_size = 
				video_fs_init_record_list(record_dir, video_record_insert_old_file, RECORD_APP_PLAN);

    anyka_print("[%s:%d] the disk free size:%u, record size:%u)\n", __func__, __LINE__,
        		pvideo_record_ctrl->record_total_size, pvideo_record_ctrl->used_record_file_size);
    //����ǰ¼����ļ��������򣬰�ʱ���Ⱥ�˳�򣬷���ѭ��¼��ʱɾ�����ļ�
    anyka_queue_sort(pvideo_record_ctrl->record_file_queue, video_record_compare_record_name);
    if(pvideo_record_ctrl->cyc_flag == 0)
    {
        if(pvideo_record_ctrl->record_total_size < MIN_DISK_SIZE_FOR_RECORD)
        {
            anyka_print("[%s:%d] the disk free size isn't enough!(%u,%d)\n", __func__, __LINE__,
					                pvideo_record_ctrl->record_total_size, MIN_DISK_SIZE_FOR_RECORD);
            goto VIDEO_RECORD_START_ERR;        
        }
    }
    else
    {
        if(pvideo_record_ctrl->record_total_size + pvideo_record_ctrl->used_record_file_size < MIN_DISK_SIZE_FOR_RECORD)
        {
            anyka_print("[%s:%d] the disk free size isn't enough!(%u,%u,%u)\n", __func__, __LINE__,
            				pvideo_record_ctrl->record_total_size, 
            				pvideo_record_ctrl->used_record_file_size, 
            				MIN_DISK_SIZE_FOR_RECORD);
            goto VIDEO_RECORD_START_ERR;        
        }
    }
    pvideo_record_ctrl->record_total_size += pvideo_record_ctrl->used_record_file_size;
    pvideo_record_ctrl->record_total_size -= MIN_DISK_SIZE_FOR_RECORD;
	
    pvideo_record_ctrl->record_time = ((pvideo_record_msg->record_plan[0].record_time) * (60*1000));
	pvideo_record_ctrl->record_end_time = 0;
    pvideo_record_ctrl->save_mux_handle = NULL;

	//��ʱrecord_dir_name ��ŵ���·��
	strcpy(pvideo_record_ctrl->record_dir_name, record_dir);
//	video_record_get_record_file_name(pvideo_record_ctrl->use_file_index);//�˴���ȡ¼��ʼʱ��
	
    //��������¼����ʱ�ļ�
    if(0 == video_record_create_record_file(0))
    {
        goto VIDEO_RECORD_START_ERR;
    }
    
    if(0 == video_record_create_record_file(1))
    {
        goto VIDEO_RECORD_START_ERR;
    }

    //��¼�񴴽������棬ɾ���ļ�������߳�
    sem_init(&pvideo_record_ctrl->sem_create_file, 0, 0);
    pthread_mutex_init(&pvideo_record_ctrl->video_record_mutex, NULL);
    if ((ret = anyka_pthread_create(&(pvideo_record_ctrl->create_record_handle_id),
									video_record_manage_file, pvideo_record_ctrl, 
									ANYKA_THREAD_NORMAL_STACK_SIZE, -1)) != 0) {
        anyka_print("[%s:%d] fail to create thread, ret = %d!\n", __func__, __LINE__, ret );
		pthread_mutex_destroy(&pvideo_record_ctrl->video_record_mutex);
        goto VIDEO_RECORD_START_ERR;
    }


    //��ʼ��¼�����
    T_MUX_INPUT mux_input;
    
    mux_input.m_MediaRecType = MEDIALIB_REC_3GP;//MEDIALIB_REC_AVI_NORMAL;
    mux_input.m_eVideoType = MEDIALIB_VIDEO_H264;
    mux_input.m_nWidth = VIDEO_WIDTH_720P;
    mux_input.m_nHeight = VIDEO_HEIGHT_720P;
    
    //mux audio
    mux_input.m_bCaptureAudio = 1;
    mux_input.m_eAudioType = MEDIALIB_AUDIO_AMR;//MEDIALIB_AUDIO_AAC;
    mux_input.m_nSampleRate = 8000;
    
    //����Ƶ����Ƶ�ϳɿ�
    pvideo_record_ctrl->mux_handle= mux_open(&mux_input, pvideo_record_ctrl->record_file[pvideo_record_ctrl->use_file_index].record_file, 
        pvideo_record_ctrl->record_file[pvideo_record_ctrl->use_file_index].index_file);
    //��ʼ��Ƶ����Ƶ��صĲɼ��߳�
    video_add(video_record_get_video_data, pvideo_record_ctrl, FRAMES_ENCODE_RECORD, pvideo_record_setting->savefilekbps);
    audio_add(SYS_AUDIO_ENCODE_AMR, video_record_get_audio_data, (void *)pvideo_record_ctrl);
    pvideo_record_ctrl->stop_flag = 0;
    anyka_print("[%s:%d] the reocrd plan is started!\n", __func__, __LINE__);

	return 1;
	
VIDEO_RECORD_START_ERR:
    video_record_del_record_file(0);
    video_record_del_record_file(1);
    pvideo_record_ctrl->run_flag = 0;
    anyka_queue_destroy(pvideo_record_ctrl->record_file_queue, video_record_free_file_queue);
    free(pvideo_record_ctrl); 
    pvideo_record_ctrl = NULL;      
    anyka_print("[%s:%d] it fails to start\n", __func__, __LINE__);
	
    return -1;
}

/**
 * NAME         video_record_stop
 * @BRIEF	¼��ֹͣ�����ͷ����е�¼����ص���Դ
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

void video_record_stop(void)
{
    if(pvideo_record_ctrl == NULL)
    {
        return;
    }
	
    pvideo_record_ctrl->run_flag = 0;
    video_del(pvideo_record_ctrl);
    audio_del(SYS_AUDIO_ENCODE_AMR,(void *)pvideo_record_ctrl);
	
    sem_post(&pvideo_record_ctrl->sem_create_file);
    pthread_join(pvideo_record_ctrl->create_record_handle_id, NULL);
    sem_destroy(&pvideo_record_ctrl->sem_create_file);

    //video_record_mutex MUST be placed after video/audio ctrl destroyed
    pthread_mutex_lock(&pvideo_record_ctrl->video_record_mutex);

	mux_close(pvideo_record_ctrl->mux_handle);
    video_record_flush_record_file(pvideo_record_ctrl->use_file_index);
    video_record_del_record_file(pvideo_record_ctrl->use_file_index?0:1);
    anyka_queue_destroy(pvideo_record_ctrl->record_file_queue, video_record_free_file_queue);
    anyka_pthread_mutex_destroy(&pvideo_record_ctrl->video_record_mutex);

    free(pvideo_record_ctrl);
	sync();
    pvideo_record_ctrl = NULL;
    anyka_print("[%s:%d] the record has stopped!\n", __func__, __LINE__);
}

/**
 * NAME         video_record_err_stop
 * @BRIEF	���¼�������ʧ�ܣ���ֹͣ¼�����ñ�־���ȴ�¼�����
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

void video_record_err_stop(void)
{
    if(pvideo_record_ctrl)
    {
        pvideo_record_ctrl->stop_flag = 1;
        while(pvideo_record_ctrl)
            usleep(10*1000);
    }
}

/**
 * NAME         video_record_main_loop
 * @BRIEF	�ƻ�¼���������رգ�ͨ���������ļ�����ʱ��Ϊ����������ֹͣ
 * @PARAM	void
 * @RETURN	void
 * @RETVAL	
 */

void video_record_main_loop(void)
{
    uint32 cur_time;
    int i = 0, record_start_flag = 0, end_time = 0, sd_in_flag = 1;
    struct tm *cur_day = anyka_fs_get_system();
    video_record_setting *sys_record_plan_info;

    sys_record_plan_info = anyka_get_record_plan();

#ifdef CONFIG_FTP_UPDATE_SUPPORT
	int ftp_time = 0;
#endif
	  
    while(1)
    {
        if(PTZ_INIT_OK == PTZControlStatus(-1))
        {
            #ifdef CONFIG_DANA_SUPPORT
            anyka_dection_init(anyka_send_video_move_alarm, NULL);
            #endif
        
            #ifdef CONFIG_TENCENT_SUPPORT
            anyka_dection_init(ak_qq_send_video_move_alarm, ak_qq_check_video_filter);
            #endif
            PTZControlStatus(PTZ_SYS_WAIT);
        }
        cur_day = anyka_fs_get_system();
        cur_time = cur_day->tm_hour * 3600 + cur_day->tm_min * 60 + cur_day->tm_sec ;
        #ifdef CONFIG_FTP_UPDATE_SUPPORT
        ftp_time ++;
        if(ftp_time == 60)
        {
            anyka_ftp_update_main(cur_day->tm_year, cur_time);
            ftp_time = 0;
        }
        #endif
        if(pvideo_record_ctrl && pvideo_record_ctrl->stop_flag)
        {
            video_record_stop();
            sleep(10);
            continue;
        }
        if(record_start_flag)
        {
            if(sys_record_plan_info->record_plan[i].active)
            {
                end_time = sys_record_plan_info->record_plan[i].end_time;
            }
            else
            {
                end_time = cur_time;
            }
        }
        if(record_start_flag && cur_time >= end_time)
        {
            anyka_print("[%s:%d] we will stop the record plan[%d], curtime:%d, endtime:%d\n", 
				__func__, __LINE__, i, cur_time, sys_record_plan_info->record_plan[i].end_time);
            record_start_flag = 0;
            video_record_stop();
        }
        else if(record_start_flag == 0)
        {
            
            for(i = 0; i < MAX_RECORD_PLAN_NUM; i++)
            {
                if(sys_record_plan_info->record_plan[i].active && 
					(sys_record_plan_info->record_plan[i].day_flag & (1 << cur_day->tm_wday)))
                {
                    if(cur_time >= sys_record_plan_info->record_plan[i].start_time &&
                        cur_time < sys_record_plan_info->record_plan[i].end_time)
                    {
                        if(check_sdcard() < 0)
                        {
                            sd_in_flag = 0;
                        }
                        else 
                        {
                            if(sd_in_flag == 0)
                            {
                                sleep(10);
                            }
                            sd_in_flag = 1;
                            if(video_record_start() >= 0)
                            {
                                record_start_flag = 1;
                                end_time = sys_record_plan_info->record_plan[i].end_time;
                                anyka_print("we will start the record plan[%d], end_time:%d\n", 
									i, sys_record_plan_info->record_plan[i].end_time);
                            }
                        }
                        break;
                        
                    }
                }
            }
        }
        else if(record_start_flag == 1)	 //the plan-record has ever been open 
        {
            if((check_sdcard() < 0))
            {
                sd_in_flag = 0;
                anyka_print("we will stop the record plan because sd is out!\n");
                record_start_flag = 0;
                video_record_stop();
            }
        }
	
	    sleep(1);
	}
} 

