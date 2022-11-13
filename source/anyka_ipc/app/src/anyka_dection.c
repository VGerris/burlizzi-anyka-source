/*******************************************************************
���ļ���������ع��ܣ�Ŀǰֻ֧���ƶ���������⡣
�ƶ������������¼���ܣ������ƶ������¼���ļ��ָ�����ƶ�һ���Ӻ��������¼��
����¼�񱣴��ļ��Ĺ����Ǵӷ���ǰVIDEO_CHECK_MOVE_IFRAMES��I֡��ʼ����û����⵽���VIDEO_CHECK_MOVE_OVER_TIME
ʱ����������䡣
*******************************************************************/

#include "common.h"
#include "anyka_alarm.h"
#include "record_replay.h"

#define VIDEO_CHECK_MOVE_IFRAMES 		2
#define MAX_ALARM_FILE_NAME_LEN        	100         //����¼���ļ���


#define ALARM_TMP_FILE_NAME 		"/mnt/tmp/"
#define AKGPIO_DEV 					"/dev/akgpio"
#define ALARM_TMP_PHOTO_DIR     	"/tmp/alarm/"
#define MIN_DISK_SIZE_FOR_ALARM		(200*1024)  //���̵ı����ռ䣬��λ��KB


/** ����¼���ļ���Ϣ��һ����ʱ�ļ���һ�����ձ�����ļ�**/
const char * alarm_record_tmp_file_name[] = 
{
	"/mnt/tmp/alarm_record_file_1",
	"/mnt/tmp/alarm_record_file_2"
};

const char * alarm_rec_tmp_index[] = 
{
	"/mnt/tmp/alarm_record_index_1",
	"/mnt/tmp/alarm_record_index_2"
};

/** ¼����Ϣ�ṹ�� **/
typedef struct _alarm_record_info
{
	uint32  file_size;	
	char    *file_name;
}alarm_record_info;

/** ¼���ļ������ṹ��**/
typedef struct alarm_record_file
{
	int index_fd;
	int record_fd;
}alarm_record_file, *Palarm_record_file;

typedef struct _video_check_move_ctrl
{
    uint8   run_flag;	//���¼�����б�־
    uint8   check_flag;	//����Ĵ�����־��0��ʾ��������
    uint8   check_type;	//�������
    uint8   save_record_flag;	//�Ƿ񱣴��ļ���־
    uint8   iframe_num;	// key ֡( I ֡)����
    uint8   move_ok;	//�Ƿ����ƶ�״̬��0��ʾδ�ƶ�
    uint8   alarm_type;	//��������
	uint8   rec_used_index;	//¼��ʹ�õĽڵ�����
	uint8   rec_save_index;	//����¼��ʹ�õĽڵ�����
    uint32  move_time;	//��⵽�쳣����ʼʱ��
    uint32  cur_time;	//��ǰʱ��
	uint32  alarm_record_total_size;	//�ܵ��ƶ����¼���ļ�ռ�õĿռ��С	
	uint32  used_alarm_record_size;		//��ǰϵͳ��ռ�õ��ƶ����¼���ļ���С
    uint16  video_level;		//��Ƶ�ȼ�
    uint16  audio_level;		//��Ƶ�ȼ�
    void    *video_queue;		//��Ƶ���ݶ���
    void    *audio_queue;		//��Ƶ���ݶ���
	void 	*record_queue;		//¼���ļ�����
	void 	*mux_handle;		//�ϳɿ�ϳɾ��
	void 	*save_mux_handle;	//����¼���õĺϳɿ���
    PA_HTHREAD  id;			
    PA_HTHREAD  save_file_id;
    pthread_mutex_t     move_mutex;	//��ģ���߳���
    sem_t   save_sem;		//��⵽�ƶ�ʱ�׵��ź�
    sem_t   data_sem;		//��ȡ������Ƶ���ݺ��׵��ź�
	sem_t   manage_sem;		//�����ļ�¼��ʱ�䵽���ź�
	char 	alarm_record_file_name[2][MAX_ALARM_FILE_NAME_LEN];		//¼���ļ�������
	alarm_record_file 	record_file[2];		//¼��ڵ���Ϣ����
    PANYKA_SEND_ALARM_FUNC *Palarm_func;	//�û��ص�
    PANYKA_FILTER_VIDEO_CHECK_FUNC *filter_check;
}video_check_move_ctrl, *Pvideo_check_move_ctrl;

static Pvideo_check_move_ctrl pvideo_move_ctrl = NULL;
int anyka_dection_pass_video_check();

/**
 * NAME         anyka_detection_wait_irq
 * @BRIEF	�ȴ�GPIO�жϵ���
 * @PARAM	int fd���򿪵��豸���
 			struct gpio_info gpio��gpio�ṹ��Ϣ
 * @RETURN	void
 * @RETVAL	
 */
static int anyka_detection_wait_irq(int fd, struct gpio_info gpio)
{
	if(ioctl(fd, SET_GPIO_IRQ, &gpio) < 0)
	{
		anyka_print("[%s:%d]set irq error.\n", __func__, __LINE__);
		return -1;

	}
	anyka_print("[%s:%d]set irq success.\n", __func__, __LINE__);

	if(ioctl(fd, LISTEN_GPIO_IRQ, (void *)gpio.pin) < 0)
	{
		anyka_print("[%s:%d]listen irq error.\n", __func__, __LINE__);
		return -2;
	}
	anyka_print("[%s:%d]get irq.\n", __func__, __LINE__);

	if(ioctl(fd, DELETE_GPIO_IRQ, (void *)gpio.pin) < 0)
	{
		anyka_print("[%s:%d]clean irq error.\n", __func__, __LINE__);
		return -3;
	}

	return 0;
}



/**
 * NAME         anyka_dection_send_alarm_info
 * @BRIEF	���;�����Ϣ��callback
 * @PARAM	void *para��Ҫ���͵Ĳ���
 			char *file_name���������ͼƬ���ļ���
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_send_alarm_info(void *para, char *file_name)
{
    if(pvideo_move_ctrl->Palarm_func)
    {   
        pvideo_move_ctrl->Palarm_func(pvideo_move_ctrl->alarm_type, 0, file_name, time(0), 1);           
    }
}


/**
 * NAME         anyka_dection_alarm
 * @BRIEF	�����������ı�����Ŀǰ��GPIO���
 * @PARAM	void
 * @RETURN	NULL
 * @RETVAL	
 */

static void *anyka_dection_alarm(void)
{
	anyka_print("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());

	int alarm_status = -1;
	int ak_alarm_fd = -1;
	struct gpio_info gpio_alarm_in;
	struct gpio_info gpio_dly_dr;

	memset(&gpio_alarm_in, 0, sizeof(struct gpio_info));
	memset(&gpio_dly_dr, 0, sizeof(struct gpio_info));
	
	gpio_alarm_in.pin		= 5,
	gpio_alarm_in.pulldown	= -1,
	gpio_alarm_in.pullup 	= AK_PULLUP_ENABLE,
	gpio_alarm_in.value		= AK_GPIO_OUT_HIGH,
	gpio_alarm_in.dir		= AK_GPIO_DIR_INPUT,
	gpio_alarm_in.int_pol	= AK_GPIO_INT_LOWLEVEL,

	gpio_dly_dr.pin        = 61,
	gpio_dly_dr.pulldown   = AK_PULLDOWN_ENABLE,
	gpio_dly_dr.pullup     = -1,
	gpio_dly_dr.value      = AK_GPIO_OUT_HIGH,
	gpio_dly_dr.dir        = AK_GPIO_DIR_OUTPUT,
	gpio_dly_dr.int_pol    = -1,


	ak_alarm_fd = open(AKGPIO_DEV, O_RDWR);
	if(ak_alarm_fd < 0)
	{
		anyka_print("[%s:%d] open ak customs device failed.\n", __func__, __LINE__);
		return NULL;
	}

	while(1){
		anyka_detection_wait_irq(ak_alarm_fd, gpio_alarm_in);     

		while(1)
		{
			ioctl(ak_alarm_fd, GET_GPIO_VALUE, &gpio_alarm_in);
			alarm_status = gpio_alarm_in.value;
			
			if(alarm_status > 0)
			{	
				/*********debounce**********/
				usleep(200);
				ioctl(ak_alarm_fd, GET_GPIO_VALUE, &gpio_alarm_in);
				alarm_status = gpio_alarm_in.value;
				
				if(alarm_status > 0)
				{
					anyka_print("[%s:%d]we check the alarm!\n", __func__, __LINE__);

					gpio_dly_dr.value = AK_GPIO_OUT_HIGH;
					ioctl(ak_alarm_fd, SET_GPIO_FUNC, &gpio_dly_dr);  
					
			        pvideo_move_ctrl->move_ok = 1;
			        pvideo_move_ctrl->alarm_type = SYS_CHECK_OTHER_ALARM;
			        pvideo_move_ctrl->move_time = time(0);
			        sem_post(&pvideo_move_ctrl->save_sem);

					sleep(2);
					gpio_dly_dr.value = AK_GPIO_OUT_LOW;
					ioctl(ak_alarm_fd, SET_GPIO_FUNC, &gpio_dly_dr);  
					
					break;
				}
			}
			else
			{
				anyka_print("[%s:%d] no alarm :%d\n", __func__, __LINE__, alarm_status);	
				break;
			}
		}
	}
	close(ak_alarm_fd);

	return NULL;
}


/**
 * NAME         anyka_dection_video_move_queue_free
 * @BRIEF	�ͷ�ָ���ڵ����Դ
 * @PARAM	void *item ��ָ��Ҫ�ͷ���Դ�Ľڵ�
                   
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_video_move_queue_free(void *item)
{
    T_STM_STRUCT *pstream = (T_STM_STRUCT *)item;

    if(pstream)
    {
        free(pstream->buf);
        free(pstream);
    }
}


/**
 * NAME         anyka_dection_video_move_queue_copy
 * @BRIEF	��������ָ�Ľڵ�����copy��һ���µĽڵ�
 * @PARAM	T_STM_STRUCT *pstream�� ��copy������
                   
 * @RETURN	T_STM_STRUCT *
 * @RETVAL	�ɹ������µ����ݽڵ�ָ�룬ʧ�ܷ��ؿ�
 */

T_STM_STRUCT * anyka_dection_video_move_queue_copy(T_STM_STRUCT *pstream)
{
    T_STM_STRUCT *new_stream;

    new_stream = (T_STM_STRUCT *)malloc(sizeof(T_STM_STRUCT));
    if(new_stream == NULL)
    {
        return NULL;
    }
    memcpy(new_stream, pstream, sizeof(T_STM_STRUCT));
    new_stream->buf = (T_pDATA)malloc(pstream->size);
    if(new_stream->buf == NULL)
    {
        free(new_stream);
        return NULL;
    }
    memcpy(new_stream->buf, pstream->buf, pstream->size);
    return new_stream;
}

/**
 * NAME         anyka_dection_move_video_data
 * @BRIEF	��Ƶ�ص�����,Ŀǰϵͳֻ��������I֡�����ݣ����Գ���
                    ��֮ǰ����Ƶ����ɾ����
 * @PARAM	param �û�����
                    pstream ��Ƶ�������
 * @RETURN	void *
 * @RETVAL	
 */

void anyka_dection_move_video_data(T_VOID *param, T_STM_STRUCT *pstream)
{
    T_STM_STRUCT *new_stream;
    uint32 last_time_stamp;
    int try_time = 0;
	
	pvideo_move_ctrl->cur_time = pstream->timestamp / 1000;
    
    if(pvideo_move_ctrl->run_flag == 0)
    {
        return;
    }
    
    pthread_mutex_lock(&pvideo_move_ctrl->move_mutex);	

    if((pstream->iFrame == 1) && (pvideo_move_ctrl->move_ok == 0))
    {
        pvideo_move_ctrl->iframe_num ++;
        if(pvideo_move_ctrl->move_ok == 0)
        {
            //���ǽ����������Ƶ����Ƶ���ݣ���Ƶ����ֻ��������
            //I,��Ƶ������Ƶ���һ��֡��ʱ�����ͬ����
            last_time_stamp = 0xFFFFFFFF;
            while(pvideo_move_ctrl->iframe_num > VIDEO_CHECK_MOVE_IFRAMES)
            {

                new_stream = (T_STM_STRUCT *)anyka_queue_pop(pvideo_move_ctrl->video_queue);

                if(new_stream == NULL)
                {
                    pvideo_move_ctrl->iframe_num = 0;
                    break;
                }
                
                last_time_stamp = new_stream->timestamp;
                if(new_stream->iFrame)
                {
                    T_STM_STRUCT * piframe_stream;
                    while(1)
                    {
                        piframe_stream = (T_STM_STRUCT *)anyka_queue_get_index_item(pvideo_move_ctrl->video_queue, 0);
                        if(piframe_stream == NULL)
                        {
                            break;
                        }
                        if(piframe_stream->iFrame)
                        {
                            break;
                        }
                        piframe_stream = (T_STM_STRUCT *)anyka_queue_pop(pvideo_move_ctrl->video_queue);
                        last_time_stamp = piframe_stream->timestamp;
                        anyka_dection_video_move_queue_free((void *)piframe_stream);
                    }
                    pvideo_move_ctrl->iframe_num --;
                }
                anyka_dection_video_move_queue_free((void *)new_stream);
            }
            //�����Ƶ���ݣ������һ�����������Ƶ���ݵ�ʱ��Ϊ׼
            if(last_time_stamp != 0xFFFFFFFF)
            {
                while(anyka_queue_not_empty(pvideo_move_ctrl->audio_queue))
                {
                    new_stream = anyka_queue_get_index_item(pvideo_move_ctrl->audio_queue, 0);
                    if(new_stream->timestamp >= last_time_stamp)
                    {
                        break;
                    }
                    new_stream = (T_STM_STRUCT *)anyka_queue_pop(pvideo_move_ctrl->audio_queue);
                    anyka_dection_video_move_queue_free((void *)new_stream);
                }
            }
        }
    }
    while(((new_stream = anyka_dection_video_move_queue_copy(pstream)) == NULL) && try_time < 10)
    {
        usleep(10*1000);
        anyka_print("we will wait for the video queue ram , it fail to  malloc!\n");
        try_time ++;
    }
    if(new_stream)
    {
        //��������������ȴ��ռ��ͷ���д����
        try_time = 0;
        while(anyka_queue_is_full(pvideo_move_ctrl->video_queue) && try_time < 10)
        {
            pthread_mutex_unlock(&pvideo_move_ctrl->move_mutex);
            usleep(10*1000);
            pthread_mutex_lock(&pvideo_move_ctrl->move_mutex);
            anyka_print("we will wait for the video queue free!\n");
            try_time ++;
        }
        
        if(anyka_queue_push(pvideo_move_ctrl->video_queue, new_stream) == 0)
        {
            anyka_print("we fails to add video data!\n");
            anyka_dection_video_move_queue_free((void *)new_stream);
        }
    }
	
    pthread_mutex_unlock(&pvideo_move_ctrl->move_mutex);
    if(pvideo_move_ctrl->move_ok)
    {
        sem_post(&pvideo_move_ctrl->data_sem);
    }
}

/**
 * NAME         anyka_dection_video_move_audio_data
 * @BRIEF	��Ƶ�ص�����
 * @PARAM	param �û�����
                    pstream ��Ƶ�������
 * @RETURN	void *
 * @RETVAL	
 */

void anyka_dection_video_move_audio_data(T_VOID *param, T_STM_STRUCT *pstream)
{
    T_STM_STRUCT *new_stream;
    
    if(pvideo_move_ctrl->run_flag == 0)
    {
        return;
    }

    pthread_mutex_lock(&pvideo_move_ctrl->move_mutex);
	/** copy the node data **/
    new_stream = anyka_dection_video_move_queue_copy(pstream);
    if(new_stream)
    {
		/** push the data to the queue **/
        if(anyka_queue_push(pvideo_move_ctrl->audio_queue, new_stream) == 0)
        {
            anyka_dection_video_move_queue_free((void *)new_stream);
        }
    }
    pthread_mutex_unlock(&pvideo_move_ctrl->move_mutex);
    if(pvideo_move_ctrl->move_ok)
    {
        sem_post(&pvideo_move_ctrl->data_sem);
    }
}

/**
 * NAME         anyka_dection_video_notice_dowith
 * @BRIEF	��鵽�ƶ�ʱ�Ļص�
 * @PARAM	user �û�����
                    notice
 
 * @RETURN	void *
 * @RETVAL	
 */

void anyka_dection_video_notice_dowith(void *user, uint8 notice)
{
    //����״��ƶ��������������ǰ�����ݡ�
    Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();
    static int send_alarm_time;

    if(pvideo_move_ctrl->check_flag == 0)
    {
        //anyka_print("[%s:%d]we will pass the moving!\n", __func__, __LINE__);
        return ;
    }
    
    if(alarm_info->alarm_send_type == 0)
    {
        
        pthread_mutex_lock(&pvideo_move_ctrl->move_mutex);  
        pvideo_move_ctrl->move_ok = 1;		//set check move flag
        //pvideo_move_ctrl->move_time = time(0);
        if(pvideo_move_ctrl->save_record_flag == 0)	//no save record , get system time
        {
            pvideo_move_ctrl->cur_time = time(0);
        }
		pvideo_move_ctrl->move_time = pvideo_move_ctrl->cur_time;	//store current time
	
        sem_post(&pvideo_move_ctrl->save_sem);		//send message to get video data thread
        pthread_mutex_unlock(&pvideo_move_ctrl->move_mutex);  
        pvideo_move_ctrl->alarm_type = SYS_CHECK_VIDEO_ALARM;	//set alarm type
        /*if not preview and trigger move detection, move_ok is set 1, goto preview and this function is trigger and take move detection. fixed it*/
		if(pvideo_move_ctrl->filter_check && (1 == pvideo_move_ctrl->filter_check()))
        {
        	return;
        }
        if((send_alarm_time + alarm_info->alarm_send_msg_time < pvideo_move_ctrl->cur_time) && (pvideo_move_ctrl->Palarm_func))
        {
            send_alarm_time = pvideo_move_ctrl->cur_time;
			/** take photos **/
            if(0 == anyka_photo_start(1, 1, ALARM_TMP_PHOTO_DIR, anyka_dection_send_alarm_info, NULL))
            {
				/** call the user callback to send message to app **/
                if(pvideo_move_ctrl->Palarm_func)
                {
                    pvideo_move_ctrl->Palarm_func(pvideo_move_ctrl->alarm_type, 0, NULL, pvideo_move_ctrl->cur_time, 1);
                }
            }
        }   
    }
   
}


/**
 * NAME         anyka_dection_remove_tmp_file
 * @BRIEF	ɾ��¼��ʹ�õ���ʱ�ļ�
 * @PARAM	char *rec_path�������ļ�·��
 			video_find_record_callback *pcallback�� �ҵ��ļ�֮�����������callback
 * @RETURN	uint64�����ط������͵��ļ����ܵ�size
 * @RETVAL	�ɹ����ط����ļ����͵��ֽڴ�С��ʧ�ܷ���-1.
 */

void anyka_dection_remove_tmp_file()
{
    remove(alarm_rec_tmp_index[0]);
    remove(alarm_rec_tmp_index[1]);
    remove(alarm_record_tmp_file_name[0]);
    remove(alarm_record_tmp_file_name[1]);
    sync();
}


/**
 * NAME         anyka_dection_flush_record_file
 * @BRIEF	¼����ɺ�ر���ʱ�ļ����ر�¼���ļ�
 * @PARAM	int index��0 ��1�������ļ��л�ʹ��
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_flush_record_file(int index)
{
    struct stat     statbuf; 
    unsigned long cur_file_size;  
    
	/******  close last index file  *******/

	if(pvideo_move_ctrl->record_file[index].index_fd > 0)
	{
		/****** close record file ******/
		close(pvideo_move_ctrl->record_file[index].index_fd);	//close index fd 
		/****** remove tmp file ******/		
		remove(alarm_rec_tmp_index[index]);
	}

	/**** close last record file , rename the tmp file name for next time record ****/
	if(pvideo_move_ctrl->record_file[index].record_fd > 0)
	{
		fsync(pvideo_move_ctrl->record_file[index].record_fd);	//sync file
		close(pvideo_move_ctrl->record_file[index].record_fd);	//close record fd
		//anyka_print("[%s:%d] alarm record file name: %s\n", __func__, __LINE__, pvideo_move_ctrl->alarm_record_file_name[index]);
		/** rename the record file **/
		rename(alarm_record_tmp_file_name[index], pvideo_move_ctrl->alarm_record_file_name[index]);		

		/** get record file information **/
        stat(pvideo_move_ctrl->alarm_record_file_name[index], &statbuf );
        cur_file_size = (statbuf.st_size >> 10) + ((statbuf.st_size & 1023)?1:0);            //�����ļ���Сϵͳ������.
        /** file size less than 100k, delete it **/
        if(cur_file_size < 100)
        {
            remove(pvideo_move_ctrl->alarm_record_file_name[index]);
        }
        else
        {	
			/** update the current file message into the record queue **/
            video_record_update_use_size(cur_file_size);
			video_fs_list_insert_file(pvideo_move_ctrl->alarm_record_file_name[index], RECORD_APP_ALARM);
        }
	}

	pvideo_move_ctrl->record_file[index].index_fd = 0;
	pvideo_move_ctrl->record_file[index].record_fd = 0;

}


/*
**	create record file and index file 
**	return val :
**	0 success, -1, failed
*/

int anyka_dection_creat_new_record_file(int index)
{
	char res[1000] = {0};

	pvideo_move_ctrl->record_file[index].record_fd = 
			open(alarm_record_tmp_file_name[index], O_RDWR| O_CREAT | O_TRUNC,S_IRUSR|S_IWUSR);
	if(pvideo_move_ctrl->record_file[index].record_fd < 0)
	{

		/** read-only file system **/
		if(EROFS == errno)
		{
			anyka_print("[%s:%d] %s, repair it ...\n", __func__, __LINE__, strerror(errno));
		
			do_syscmd("mount | grep '/dev/mmcblk0p1 on /mnt'", res);
			if(strlen(res) > 0) {
				anyka_print("[%s:%d] remount /dev/mmcblk0p1\n", __func__, __LINE__);
				system("mount -o remount,rw /dev/mmcblk0p1");
				return -1;
			}
		
			bzero(res, 1000);
	
			do_syscmd("mount | grep '/dev/mmcblk0 on /mnt'", res);
			if(strlen(res) > 0) {
				anyka_print("[%s:%d] remount /dev/mmcblk0\n", __func__, __LINE__);
				system("mount -o remount,rw /dev/mmcblk0");
				return -1;
			}
		
		} 
		else
			anyka_print("[%s:%d] %s\n", __func__, __LINE__, strerror(errno));
		return -1;	
		
	}

	pvideo_move_ctrl->record_file[index].index_fd = open(alarm_rec_tmp_index[index], O_RDWR| O_CREAT | O_TRUNC,S_IRUSR|S_IWUSR);
	if(pvideo_move_ctrl->record_file[index].index_fd < 0)
	{
		anyka_print("[%s:%d] open file %s failed, :%s\n", __func__, __LINE__, 
						alarm_rec_tmp_index[index], strerror(errno));
		close(pvideo_move_ctrl->record_file[index].record_fd);
		pvideo_move_ctrl->record_file[index].record_fd = 0;
		remove(alarm_record_tmp_file_name[index]);
		return -1;
	}
	
	return 0;
}

/**
 * NAME         anyka_dection_free_file_queue
 * @BRIEF	�ͷŶ��г�Ա��Դ
 * @PARAM	void *item��Ҫ�ͷŵ���Դ�ڵ�
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_free_file_queue(void *item)
{
	alarm_record_info *file_info = (alarm_record_info*)item;

	if(file_info)
	{
		free(file_info->file_name);
		free(file_info);
	}
}

/**
 * NAME         anyka_dection_malloc_file_queue
 * @BRIEF	������нڵ�������ڴ�
 * @PARAM	int file_size�� Ҫ������ļ�����
 * @RETURN	alarm_record_info *
 * @RETVAL	�ɹ�: ����ָ��������ݽڵ��ָ��
 			ʧ��: ���ؿ�
 */

alarm_record_info *anyka_dection_malloc_file_queue(int file_len)
{    
	alarm_record_info *file_info = NULL;

	file_info = (alarm_record_info *)malloc(sizeof(alarm_record_info));
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
 * NAME         anyka_dection_insert_file
 * @BRIEF	���ļ����뵽ָ���Ķ�����
 * @PARAM	char *file_name�� Ҫ������ļ���
 			int file_size�� ҪҪ����ļ��Ĵ�С
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_insert_file(char *file_name, int file_size,int type)
{
	int file_len;    
	alarm_record_info * pnew_record;

	file_len = strlen(file_name);
	pnew_record = anyka_dection_malloc_file_queue(file_len + 4);
	pnew_record->file_size = file_size;
	strcpy(pnew_record->file_name, file_name);
	if(0 == anyka_queue_push(pvideo_move_ctrl->record_queue, (void *)pnew_record))
	{
		anyka_dection_free_file_queue(pnew_record);
	}
}

/**
 ** NAME    anyka_dection_compare_record_name
 ** @BRIEF	compare two record file name
 ** @PARAM	void
 ** @RETURN	void
 ** @RETVAL	
 **/

int anyka_dection_compare_record_name(void *item1, void *item2)
{
	alarm_record_info *precord1 = (alarm_record_info *)item1;
	alarm_record_info * precord2 = (alarm_record_info *)item2;
	int len1, len2;

	len1 = strlen(precord1->file_name);
	len2 = strlen(precord2->file_name);

	return memcmp(precord1->file_name, precord2->file_name, len1>len2?len1:len2); 
}

/**
 * NAME     anyka_dection_manage_file
 * @BRIEF	save record file
 * @PARAM	void *, now no use
 * @RETURN	void *
 * @RETVAL	
 */

void *anyka_dection_manage_file(void * arg)
{	
	anyka_print("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());

	char record_file_name[100];
	int create_file_ret;
	Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();
	alarm_record_info *alarm_remove_file;
	
	while(pvideo_move_ctrl->run_flag)
	{
		sem_wait(&pvideo_move_ctrl->manage_sem);

		uint32 cur_file_size;
		struct stat  statbuf;
		/**** send alarm message to the phone or computer ****/
		if(pvideo_move_ctrl->save_mux_handle)
		{
			anyka_print("[%s:%d] Close last handle, save file!\n", __func__, __LINE__);

			//uint32 cur_file_size;
			//struct stat  statbuf;

			/************ close mux handle *************/
			mux_close(pvideo_move_ctrl->save_mux_handle);

			/***** get record file name *****/
	        video_fs_get_alarm_record_name(pvideo_move_ctrl->alarm_type, 
					alarm_info->alarm_default_record_dir, record_file_name, ".mp4");

			/** copy the file name to the array **/
			strcpy(pvideo_move_ctrl->alarm_record_file_name[pvideo_move_ctrl->rec_save_index], record_file_name);

			/************* get_cur_file_index ***************/
            anyka_print("[%s:%d] Now save file[%d]: %s\n", __func__, __LINE__,
            		pvideo_move_ctrl->rec_save_index, pvideo_move_ctrl->alarm_record_file_name[pvideo_move_ctrl->rec_save_index]);

			/********* close the last file , open a new file  *********/
			anyka_dection_flush_record_file(pvideo_move_ctrl->rec_save_index);

			create_file_ret = anyka_dection_creat_new_record_file(pvideo_move_ctrl->rec_save_index);
            if(create_file_ret < 0)
            {
				remove(record_file_name);
                anyka_print("[%s:%d] It fails to create record file!\n", __func__, __LINE__);
            }
			
			sync();
			pvideo_move_ctrl->save_mux_handle = NULL;
		}

		stat(pvideo_move_ctrl->alarm_record_file_name[pvideo_move_ctrl->rec_save_index], &statbuf);
		cur_file_size = (statbuf.st_size >> 10) + ((statbuf.st_size & 1023)?1:0);
		anyka_dection_insert_file(record_file_name, cur_file_size,RECORD_APP_ALARM);
		pvideo_move_ctrl->used_alarm_record_size += cur_file_size;

		/***** if the room is not enough, delete the earliest record file *****/
		while(pvideo_move_ctrl->used_alarm_record_size >= pvideo_move_ctrl->alarm_record_total_size)
		{
			alarm_remove_file = (alarm_record_info *)anyka_queue_pop(pvideo_move_ctrl->record_queue);
			if(alarm_remove_file == NULL)
			{
				break;
			}
			anyka_print("[%s] room isn't enough, remove: %s, size:%d\n",
					__func__, alarm_remove_file->file_name, 
					pvideo_move_ctrl->used_alarm_record_size);				
			remove(alarm_remove_file->file_name);   
			pvideo_move_ctrl->used_alarm_record_size -= alarm_remove_file->file_size;           
			anyka_dection_free_file_queue(alarm_remove_file);
		}

	}
	anyka_print("[%s:%d] save the lasted alarm record finish.\n", __func__, __LINE__);

	return NULL;
}

/**
 ** NAME    anyka_dection_free_record_info
 ** @BRIEF	when check the SD card is out, we clean the record info
 ** @PARAM	void
 ** @RETURN	void 
 ** @RETVAL	
 **/
void anyka_dection_free_record_info(void)
{
	pvideo_move_ctrl->alarm_record_total_size = 0;
	pvideo_move_ctrl->used_alarm_record_size = 0;
	
	void *tmp = pvideo_move_ctrl->record_queue;
	pvideo_move_ctrl->record_queue = NULL;
	anyka_queue_destroy(tmp, anyka_dection_free_file_queue);
}


/**
 ** NAME    anyka_dection_get_record_info
 ** @BRIEF	create record queue for manage record file, then get the SD card record info
 ** @PARAM	void 
 ** @RETURN	int
 ** @RETVAL	-1, faild; 0 success
 **/
int anyka_dection_get_record_info(void)
{
    Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();

    if(NULL == pvideo_move_ctrl || (pvideo_move_ctrl->record_queue))
    {
		anyka_print("[%s:%d] Please initialize it at first!\n", __func__, __LINE__);	
        return 0;
    }
    
	pvideo_move_ctrl->record_queue = anyka_queue_init(2500);
	if(pvideo_move_ctrl->record_queue == NULL)
	{
		anyka_print("[%s:%d] it fails to init record queue\n", __func__, __LINE__);	
		anyka_queue_destroy(pvideo_move_ctrl->record_queue, anyka_dection_free_file_queue);
		return -1;
	}

	/*** initialize two wariable to record the space which can using for this model ***/
	pvideo_move_ctrl->alarm_record_total_size = video_fs_get_free_size(alarm_info->alarm_default_record_dir);
	pvideo_move_ctrl->used_alarm_record_size = video_fs_init_record_list(alarm_info->alarm_default_record_dir, 
															anyka_dection_insert_file, RECORD_APP_ALARM);
		
	/*** sort the record list ***/
	anyka_queue_sort(pvideo_move_ctrl->record_queue, anyka_dection_compare_record_name);

	/*** total record size is equal the remainder space plus ago record file occupy space ***/
	pvideo_move_ctrl->alarm_record_total_size += pvideo_move_ctrl->used_alarm_record_size;
	if(pvideo_move_ctrl->alarm_record_total_size < MIN_DISK_SIZE_FOR_ALARM)
	{
		anyka_print("[%s:%d] the SD card room is not enough, please clean it.\n", __func__, __LINE__);
		return -1;
	}
	pvideo_move_ctrl->alarm_record_total_size -= MIN_DISK_SIZE_FOR_ALARM; //reverve 100M size for others
	
	if(pvideo_move_ctrl->alarm_record_total_size <= pvideo_move_ctrl->used_alarm_record_size)
		sem_post(&pvideo_move_ctrl->manage_sem);

	anyka_print("[%s:%d] get record info, total size : %u, used size : %u\n", __func__, __LINE__,
				pvideo_move_ctrl->alarm_record_total_size, pvideo_move_ctrl->used_alarm_record_size);

	return 0;
}


/**
 ** NAME 	anyka_dection_start_save_file  
 ** @BRIEF	when check sd_card insert to system, we create file and get the record info from sd_card
 ** @PARAM	void  
 ** @RETURN	void 
 ** @RETVAL	
 **/

int anyka_dection_start_save_file(void)
{
    int first_time_stamp;
    T_STM_STRUCT *pvideo_stream, *paudio_stream;
    Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();

    anyka_print("[%s:%d] we will start save the alarm file!\n", __func__, __LINE__);
    video_fs_create_dir(ALARM_TMP_FILE_NAME);
    video_fs_create_dir(alarm_info->alarm_default_record_dir);

	if(anyka_dection_get_record_info() < 0)
	{
		anyka_print("[%s:%d] The SD_card room no enough, we don't start record\n",
					 __func__, __LINE__);
		return -1;
	}

    /************ sync video and audio *********************/
    first_time_stamp = -1;

    /************ we will lost p frame at first ***********/
    while(anyka_queue_not_empty(pvideo_move_ctrl->video_queue))
    {
        pvideo_stream = (T_STM_STRUCT *)anyka_queue_get_index_item(pvideo_move_ctrl->video_queue, 0);
        if(pvideo_stream == NULL)
        {
            break;
        }
        first_time_stamp = pvideo_stream->timestamp;
        if(pvideo_stream->iFrame)
        {
            break;
        }
        pvideo_stream = (T_STM_STRUCT *)anyka_queue_pop(pvideo_move_ctrl->video_queue);
        anyka_dection_video_move_queue_free((void *)pvideo_stream);
    }
    /******* lost the audio frame, before the first I frame ******/
    if(first_time_stamp != -1)
    {
        while(anyka_queue_not_empty(pvideo_move_ctrl->audio_queue))
        {
            paudio_stream = (T_STM_STRUCT *)anyka_queue_get_index_item(pvideo_move_ctrl->audio_queue, 0);
            if(paudio_stream == NULL)
            {
                break;
            }
            if(first_time_stamp <= paudio_stream->timestamp)
            {
                break;
            }
            paudio_stream = (T_STM_STRUCT *)anyka_queue_pop(pvideo_move_ctrl->audio_queue);
            anyka_dection_video_move_queue_free((void *)paudio_stream);
        }
    }
    pvideo_move_ctrl->rec_save_index = 0;
    pvideo_move_ctrl->rec_used_index = 0;
	/***** open the first record and index file *****/
	if(anyka_dection_creat_new_record_file(0) < 0)
	{
        anyka_print("[%s:%d] It fail to create the first record file!\n", __func__, __LINE__);
	}
	if(anyka_dection_creat_new_record_file(1) < 0)
	{
        anyka_print("[%s:%d] It fail to create the first record file!\n", __func__, __LINE__);
	}

	return 0;
	
}


/**
 ** NAME    anyka_dection_move_get_video_data
 ** @BRIEF	when check moving or other alarm, start get video data, open mux, add data to mux
 ** @PARAM	void * user, now no use
 ** @RETURN	void *
 ** @RETVAL	NULL
 **/

void *anyka_dection_record_handler(void *user)
{
	anyka_print("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());
    T_STM_STRUCT *pvideo_stream, *paudio_stream;
    uint32 cur_time, send_msg_time = 0, send_alarm_time = 0;
    int sd_ok = 1; //SD card status
    int sd_room = 1;
	Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();

	T_MUX_INPUT mux_input;    
	mux_input.m_MediaRecType = MEDIALIB_REC_3GP;	//MEDIALIB_REC_AVI_NORMAL;
	mux_input.m_eVideoType = MEDIALIB_VIDEO_H264;
	mux_input.m_nWidth = VIDEO_WIDTH_720P;
	mux_input.m_nHeight = VIDEO_HEIGHT_720P;
	//mux audio
	mux_input.m_bCaptureAudio = 1;
	mux_input.m_eAudioType = MEDIALIB_AUDIO_AMR;	//MEDIALIB_AUDIO_AAC;
	mux_input.m_nSampleRate = 8000;

    sd_ok = sd_get_status(); //get card status
    if(1 == sd_ok) //card is in
    {
        if (anyka_dection_start_save_file() < 0) {
			sd_room = 0;	//room not enough to start record
		}
    }
        
    while(pvideo_move_ctrl->run_flag)
    {
        sem_wait(&pvideo_move_ctrl->save_sem); //�ȴ�����źŲ�����¼��
        
        cur_time = pvideo_move_ctrl->cur_time;
        send_alarm_time = 0;
		if((pvideo_move_ctrl->move_ok == 0) || 
			(pvideo_move_ctrl->move_time + (alarm_info->alarm_move_over_time) < cur_time))
        {
            //anyka_print("[%s:%d] it pass the moving signal!\n", __func__, __LINE__);
            continue;
        }

#ifdef STOP_DECT_RECORD   //֧����ͣ���¼��
        if (pvideo_move_ctrl->check_flag == 0)
            continue;
#endif        
		anyka_print("[%s:%d] move start time: %u, record time: %d, current time: %d\n", 
				 __func__, __LINE__, pvideo_move_ctrl->move_time , alarm_info->alarm_record_time, cur_time);
		
#ifdef STOP_DECT_RECORD   //֧����ͣ���¼��
		while(pvideo_move_ctrl->run_flag && pvideo_move_ctrl->check_flag)
#else		
		while(pvideo_move_ctrl->run_flag)
#endif
		{
	        sem_wait(&pvideo_move_ctrl->data_sem); //�ȴ�����Ƶ�������ݵ������muxer�ϳ�

			/*
			** The distance from the most recent to detect anomaly time, 
			** plus the timeout still no exception, then stop the current mobile detection. 
			*/
	        cur_time = pvideo_move_ctrl->cur_time;
			if(pvideo_move_ctrl->move_time + (alarm_info->alarm_move_over_time) < cur_time)
	        {
	            anyka_print("[%s:%d] system detected there is no move. Current time: [%d], start time: [%d]!\n", 
							__func__, __LINE__, cur_time, pvideo_move_ctrl->move_time);
	            break;
	        }
            
	        while(pvideo_move_ctrl->run_flag &&
				(anyka_queue_not_empty(pvideo_move_ctrl->video_queue) || 
				 anyka_queue_not_empty(pvideo_move_ctrl->audio_queue))) {
				/*
				 *  mux main loop
				 */

	            //cur_time = time(0);  
	            cur_time = pvideo_move_ctrl->cur_time;
                #if 0
                if((send_alarm_time + alarm_info->alarm_send_msg_time < cur_time) 
						&& (pvideo_move_ctrl->Palarm_func)) {
                    send_alarm_time = cur_time;
                    if(0 == anyka_photo_start(1, 1, alarm_info->alarm_default_record_dir,
							   	anyka_dection_send_alarm_info, NULL)) {
                        if(pvideo_move_ctrl->Palarm_func) {
                            pvideo_move_ctrl->Palarm_func(pvideo_move_ctrl->alarm_type, 0, NULL, cur_time, 1);
                        }
                    }
                }   
                #endif
	            if (sd_get_status() == 0) { //T���γ� 
                    if (sd_ok == 1) {
	                    sd_ok = 0;
						anyka_dection_free_record_info();
                        pvideo_move_ctrl->save_mux_handle = pvideo_move_ctrl->mux_handle;
                        pvideo_move_ctrl->mux_handle =  NULL;
                        sem_post(&pvideo_move_ctrl->manage_sem);
                    }
	            } else {		//T������ 
                    if (sd_ok == 0)
                    {
                        if(anyka_dection_start_save_file() < 0){
							sd_room = 0;	//room not enough to start record
                        }
                        sd_ok = 1;
                    }
				}

	            pvideo_stream = (T_STM_STRUCT *)anyka_queue_pop(pvideo_move_ctrl->video_queue);
	            if(pvideo_stream) {
    				/**** ���¼��ʱ������I֡��ʱ��������зָ�ﵽalarm_record_time�ͽ��зָ� ****/
                    if(pvideo_stream->iFrame)
                    {
                        cur_time = pvideo_stream->timestamp / 1000;
						/*
						 * according to tf card status and record time to decide start new mux add work
						 */
                        if((sd_ok == 1) && (pvideo_move_ctrl->mux_handle != NULL) && (sd_room == 1) &&
							(cur_time >= send_msg_time + (alarm_info->alarm_record_time))) {
                             
                            /***** change mux handle and index handle  *****/
                            pvideo_move_ctrl->rec_save_index = pvideo_move_ctrl->rec_used_index;
                            pvideo_move_ctrl->save_mux_handle = pvideo_move_ctrl->mux_handle;
                            pvideo_move_ctrl->rec_used_index = pvideo_move_ctrl->rec_used_index ? 0 : 1;
                            anyka_print("[%s:%d] using index exchange, now using handle :%d\n",
								   		 __func__, __LINE__, pvideo_move_ctrl->rec_used_index);
                        
                            pvideo_move_ctrl->mux_handle =  NULL;
                            sem_post(&pvideo_move_ctrl->manage_sem);
                        }
						/*
						 * open mux, start mux add
						 */
    					if ((sd_ok == 1) && (sd_room == 1) && (pvideo_move_ctrl->mux_handle == NULL)) {
     			            pvideo_move_ctrl->mux_handle = mux_open(&mux_input, 
    							pvideo_move_ctrl->record_file[pvideo_move_ctrl->rec_used_index].record_fd, 
    							pvideo_move_ctrl->record_file[pvideo_move_ctrl->rec_used_index].index_fd);
                      
                            send_msg_time = pvideo_stream->timestamp / 1000;
    					}
                    }

	                if (pvideo_stream->iFrame) {
	                    pthread_mutex_lock(&pvideo_move_ctrl->move_mutex);
                        if(pvideo_move_ctrl->iframe_num) {
	                        pvideo_move_ctrl->iframe_num --;
                        }
	                    pthread_mutex_unlock(&pvideo_move_ctrl->move_mutex);
	                }
                    if (mux_addVideo(pvideo_move_ctrl->mux_handle, pvideo_stream->buf, pvideo_stream->size, 
						pvideo_stream->timestamp, pvideo_stream->iFrame) <  0) {
                        video_set_iframe(pvideo_move_ctrl);
                        //now do nothing
                    }
	                anyka_dection_video_move_queue_free((void *)pvideo_stream);
	            }
	            
	            paudio_stream = (T_STM_STRUCT *)anyka_queue_pop(pvideo_move_ctrl->audio_queue);
	            if (paudio_stream) {
                    if (mux_addAudio(pvideo_move_ctrl->mux_handle, paudio_stream->buf, paudio_stream->size, 
									paudio_stream->timestamp) < 0) {
						//now do nothing
                    }
                	anyka_dection_video_move_queue_free((void *)paudio_stream);
            	}
        	}
        }

        /*
            �˴������жϵ�ǰ��û������ǰһ�ηָ��ļ��ı��涯����
            ��Ҫ�ȵ�ǰһ�ηָ��ļ��������(��save_mux_handle��Ϊ��)��������ǰ¼��ı��涯����
        */
        while (pvideo_move_ctrl->save_mux_handle != NULL)
            usleep(100*1000);

        //�����̽��������浱ǰ¼�Ƶ��ļ�
        pvideo_move_ctrl->move_ok = 0;
        pvideo_move_ctrl->rec_save_index = pvideo_move_ctrl->rec_used_index;
        pvideo_move_ctrl->save_mux_handle = pvideo_move_ctrl->mux_handle;
        pvideo_move_ctrl->rec_used_index = pvideo_move_ctrl->rec_used_index ? 0 : 1; //��¼���ļ�index�л�
        pvideo_move_ctrl->mux_handle =  NULL;
        sem_post(&pvideo_move_ctrl->manage_sem);
        
        anyka_print("\n[%s:%d]### The alarming is over, Please check the record!\n", __func__, __LINE__);
    }
    anyka_print("[%s:%d] It exit, id : %ld\n", __func__, __LINE__, (long int)gettid());

	return NULL;	
}



/**
 * NAME         anyka_dection_audio_voice_data
 * @BRIEF	��������߼�
 * @PARAM	
 
 * @RETURN	void
 * @RETVAL	
 */

int anyka_dection_audio_voice_data(void *pbuf, int len, int valve)
{
    sint16 *pdata = (T_S16 *)pbuf;
    int count = len / 32;
    int i;
    int average = 0;
    sint16 temp;
    sint32 DCavr = 0;
    static sint32 DCValue = 0;
    static uint32 checkCnt = 0;

    
    //caculte direct_current value
    if (checkCnt < 2)  
    {
        //��ֹ��ʼ¼����ѹ���ȶ�����ʼ��֡������ֱ��ƫ�Ƶļ���
        checkCnt ++;
    }
    else if (checkCnt <= 20)
    {
    //���ǵ�Ӳ���ٶȣ�ֻ��3��20֡�����ݲ���ֱ��ƫ�Ƶļ���
        checkCnt ++;
        for (i=0; i<len/2; i++)
        {
            DCavr += pdata[i];
        }
        DCavr /= (signed)(len/2);
        DCavr += DCValue;
        DCValue = (T_S16)(DCavr/2);
    }
    // spot check data value
    for (i = 0; i < count; i++)
    {
        temp = pdata[i*16];
        temp = (T_S16)(temp - DCValue);
        if (temp < 0)
        {
            average += (-temp);
        }
        else
        {
            average += temp;
        }
    }
    average /= count;
    if (average < valve)
    {
        return AK_FALSE;
    }
    else
    {
        return AK_TRUE;
    }
}



/**
 * NAME         anyka_dection_add_voice_check
 * @BRIEF	���������Ƶ���ݻص�
 * @PARAM	
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_audio_get_pcm_data(void *param, T_STM_STRUCT *pstream)
{
    static int send_alarm_time = 0;
    
    if(pvideo_move_ctrl->check_flag == 0)
    {
        anyka_print("[%s:%d] we will pass the voice checking!\n", __func__, __LINE__);
        return ;
    }

    if(anyka_dection_audio_voice_data(pstream->buf, pstream->size, pvideo_move_ctrl->audio_level))
    {
        Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();	//get system alamr info
        //����״��ƶ��������������ǰ�����ݡ�
        anyka_print("[%s:%d] we check the voice!\n", __func__, __LINE__);
        if(alarm_info->alarm_send_type == 0)
        {
            pvideo_move_ctrl->move_ok = 1;
            pvideo_move_ctrl->alarm_type = SYS_CHECK_VOICE_ALARM;
            pvideo_move_ctrl->move_time = time(0);
            sem_post(&pvideo_move_ctrl->save_sem);      //�����ź�֪ͨ����¼���߳�
            if((send_alarm_time + alarm_info->alarm_send_msg_time < pvideo_move_ctrl->cur_time) && (pvideo_move_ctrl->Palarm_func))
            {
                send_alarm_time = pvideo_move_ctrl->cur_time;
				/** take photos, send alarm to phone app **/
                if(0 == anyka_photo_start(1, 1, ALARM_TMP_PHOTO_DIR, anyka_dection_send_alarm_info, NULL))
                {
                    if(pvideo_move_ctrl->Palarm_func)	//call this callback send message
                    {
                        pvideo_move_ctrl->Palarm_func(pvideo_move_ctrl->alarm_type, 0, NULL, pvideo_move_ctrl->cur_time, 1);
                    }
                }
            }
        }
    }
}


/**
 * NAME         anyka_dection_add_voice_check
 * @BRIEF	���ƶ���⹦��
 * @PARAM	move_level ���ı�׼
 * @RETURN	void
 * @RETVAL	
 */

void check_move_add_video_check(int move_level)
{
    uint16 i, ratios=100, Sensitivity[65];
    T_MOTION_DETECTOR_DIMENSION detection_pos;
    Psystem_alarm_set_info alarm = anyka_sys_alarm_info();	//get system alamr info

    move_level --;    
    switch(move_level)
    {
        case 0:
        {
            ratios = alarm->motion_detection_1;
            break;
        }
        case 1:
        {
            ratios = alarm->motion_detection_2;
            break;
        }
        case 2:
        {
            ratios = alarm->motion_detection_3;
            break;
        }       
    }
    
    for(i = 0; i < 65; i++)
    {
        Sensitivity[i] = ratios;
    }
	//�ָ�ͼ��Ϊmotion_size_x * motion_size_y ��
    detection_pos.m_uHoriNum = alarm->motion_size_x;
    detection_pos.m_uVeriNum = alarm->motion_size_y;
	/** start video move check func **/
	if(video_start_move_check(VIDEO_HEIGHT_VGA, VIDEO_WIDTH_VGA, &detection_pos, Sensitivity, alarm->alarm_interval_time, anyka_dection_video_notice_dowith, NULL, anyka_dection_pass_video_check) == 0)
    {
        pvideo_move_ctrl->check_type &= ~SYS_CHECK_VIDEO_ALARM;
    }
    else
    {
        pvideo_move_ctrl->check_type |= SYS_CHECK_VIDEO_ALARM;
    }
    pvideo_move_ctrl->video_level = ratios;
}

/**
 * NAME         anyka_dection_add_voice_check
 * @BRIEF	��������⹦��
 * @PARAM	move_level ���ı�׼
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_add_voice_check(int move_level)
{
    Psystem_alarm_set_info alarm = anyka_sys_alarm_info(); //get system alamr info
    uint16 ratios=10;
        
    move_level --;
    switch(move_level)
    {
        case 0:
        {
            ratios = alarm->opensound_detection_1;
            break;
        }
        case 1:
        {
            ratios = alarm->opensound_detection_2;
            break;
        }
        case 2:
        {
            ratios = alarm->opensound_detection_3;
            break;
        }       
    }
    pvideo_move_ctrl->audio_level = ratios;     

	/** add audio **/
    if(-1 == audio_add(SYS_AUDIO_RAW_PCM, anyka_dection_audio_get_pcm_data, (void *)pvideo_move_ctrl))
    {
        pvideo_move_ctrl->check_type &= ~SYS_CHECK_VOICE_ALARM;
    }
    else
    {
        pvideo_move_ctrl->check_type |= SYS_CHECK_VOICE_ALARM;
    }
}





/**
 * NAME         anyka_dection_start
 * @BRIEF	����⹦��
 * @PARAM	move_level ���ı�׼
                    check_type �������
                    Palarm_func ���ص�����
                    filter_check   ��ǰ�Ƿ�������ģ�Ŀǰ�����Ѷ������Ƶ�ۿ���ʱ�������
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_start(int move_level, int check_type, PANYKA_SEND_ALARM_FUNC Palarm_func, PANYKA_FILTER_VIDEO_CHECK_FUNC filter_check)
{	
	pthread_t alarm_pid;
    Psystem_alarm_set_info alarm_info = anyka_sys_alarm_info();	//get system alamr info
	video_setting * pvideo_record_setting = anyka_get_sys_video_setting();

    if(move_level > 2)
    {
        move_level = 2;
    }
    
    if(check_type == 0) //unsupported type
    {
        return;
    }
    
    if(pvideo_move_ctrl)
    {
        if(pvideo_move_ctrl->check_type & check_type) //current check type has been opened.
        {
            anyka_print("[%s:%d] it fails to start because it has runned!\n", __func__, __LINE__);
            return ;
        }
        if(check_type & SYS_CHECK_VOICE_ALARM) //add voice check
        {
            anyka_print("[%s:%d] it will start voice check!\n", __func__, __LINE__);
            anyka_dection_add_voice_check(move_level); //voice check entry
        }
        if(check_type & SYS_CHECK_VIDEO_ALARM) //add video check
        {
            anyka_print("[%s:%d] it will start video check!\n", __func__, __LINE__);
            check_move_add_video_check(move_level);  //video check entry
        }
		if(check_type & SYS_CHECK_OTHER_ALARM) //add other check, now use gpio check
		{
		    anyka_print("[%s:%d] it will start other check!\n", __func__, __LINE__);
			/** gpio alarm check entry **/
			anyka_pthread_create(&alarm_pid, (anyka_thread_main *)anyka_dection_alarm, (void *)NULL,
									ANYKA_THREAD_MIN_STACK_SIZE, -1);
		}
        
        return;
    }
    
	/*** create temp directory, now is : /mnt/tmp/ ***/
    video_fs_create_dir(ALARM_TMP_FILE_NAME); 

	/*** create save record file directory according to the ini file setting ***/
    video_fs_create_dir(alarm_info->alarm_default_record_dir); 
	
	/*** initialize the main handle ***/
    pvideo_move_ctrl = (Pvideo_check_move_ctrl)malloc(sizeof(video_check_move_ctrl));
    if(pvideo_move_ctrl == NULL)
    {
        anyka_print("[%s:%d] it fail to malloc!\n", __func__, __LINE__);
        return;
    }
    memset(pvideo_move_ctrl, 0, sizeof(video_check_move_ctrl));

    pvideo_move_ctrl->filter_check = filter_check;
    pvideo_move_ctrl->Palarm_func = Palarm_func; //alarm call back, use to send message to the phone or PC
	pvideo_move_ctrl->mux_handle = NULL; //mux handle, use to open mux lib, add video and audio data to the mux lib
	pvideo_move_ctrl->save_mux_handle = NULL; //save muxed file handle
	pvideo_move_ctrl->check_type = 0; //clear check type
	pvideo_move_ctrl->rec_used_index = 0; //current record used index
	pvideo_move_ctrl->save_record_flag = alarm_info->alarm_save_record;
	
	/*** initialize three semaphore and one thread mutex locker ***/
	/*** according to different type to start respectively check handle ***/
    if(check_type & SYS_CHECK_VIDEO_ALARM) //video check 
    {
        check_move_add_video_check(move_level);//video check entry
    }
    
    if(check_type & SYS_CHECK_VOICE_ALARM) //voice check
    {
        anyka_dection_add_voice_check(move_level);//add voice check
    }
	if(check_type & SYS_CHECK_OTHER_ALARM) //other type check
	{
	    anyka_print("[%s:%d] it will start other check!\n", __func__, __LINE__);
		anyka_pthread_create(&alarm_pid, (anyka_thread_main *)anyka_dection_alarm, (void *)NULL,
								ANYKA_THREAD_MIN_STACK_SIZE, -1);
	}

    pvideo_move_ctrl->run_flag = 1; //set this module run flag 
    pvideo_move_ctrl->check_flag = 1;
    pvideo_move_ctrl->iframe_num = 0; //clear key frame total number
    pvideo_move_ctrl->move_ok = 0; //clear moving or voice or other abnormal phenomena has been checked flag
	pvideo_move_ctrl->cur_time = 0; //clear current time, used to control the record time
    pthread_mutex_init(&pvideo_move_ctrl->move_mutex, NULL); 

	/** Save record flag is true, then init some val **/
    if(pvideo_move_ctrl->save_record_flag)	
    {
        sem_init(&pvideo_move_ctrl->data_sem, 0, 0);
        sem_init(&pvideo_move_ctrl->save_sem, 0, 0);
        sem_init(&pvideo_move_ctrl->manage_sem, 0, 0);
        
    	/**** initialize three queue ****/
        pvideo_move_ctrl->video_queue = anyka_queue_init(100);
        pvideo_move_ctrl->audio_queue = anyka_queue_init(200);
        if((pvideo_move_ctrl->video_queue == NULL) || ( pvideo_move_ctrl->audio_queue == NULL))
        {
            goto ERR_video_check_move_start;
        }

    	/*** alarm_send_type = 0, record model, alarm_send_type = 1, photograph model ***/
        if(alarm_info->alarm_send_type != 1) //record model 
        {
			/** add video get data **/
            video_add(anyka_dection_move_video_data, (void *)pvideo_move_ctrl, FRAMES_ENCODE_RECORD, 
            	pvideo_record_setting->savefilekbps);
			/** add audio get data **/
            audio_add(SYS_AUDIO_ENCODE_AMR, anyka_dection_video_move_audio_data, (void *)pvideo_move_ctrl);
        }
    	/**** create get video data thread, the thread wait for semaphore, than open mux and add data ****/
    	if(anyka_pthread_create(&(pvideo_move_ctrl->id), anyka_dection_record_handler,
    							(void *)pvideo_move_ctrl, ANYKA_THREAD_NORMAL_STACK_SIZE, -1) != 0){
    		anyka_print("[%s:%d] it fail to create thread!\n", __func__, __LINE__);
    		goto ERR_video_check_move_start;
    	}
    	/****** create save alarm record file thread *****/
    	if(anyka_pthread_create(&(pvideo_move_ctrl->save_file_id), anyka_dection_manage_file,
    							(void *)pvideo_move_ctrl, ANYKA_THREAD_NORMAL_STACK_SIZE, -1) != 0){
    		anyka_print("[%s:%d] it fail to create thread!\n", __func__, __LINE__);
    		goto ERR_video_check_move_start;
    	}
    }

	return ;
	
ERR_video_check_move_start:

	/** release the resource  **/
    if(pvideo_move_ctrl->save_record_flag)
    {
        video_del(pvideo_move_ctrl);
        audio_del(SYS_AUDIO_ENCODE_AMR, pvideo_move_ctrl);
        anyka_queue_destroy(pvideo_move_ctrl->video_queue, anyka_dection_video_move_queue_free);
        anyka_queue_destroy(pvideo_move_ctrl->audio_queue, anyka_dection_video_move_queue_free);
        sem_destroy(&pvideo_move_ctrl->data_sem);
        sem_destroy(&pvideo_move_ctrl->save_sem);
        sem_destroy(&pvideo_move_ctrl->manage_sem);
    }
    
    anyka_pthread_mutex_destroy(&pvideo_move_ctrl->move_mutex); 
    free(pvideo_move_ctrl);
    pvideo_move_ctrl = NULL;
    return ;
    
}

/**
 * NAME         anyka_dection_stop
 * @BRIEF	�ر���Ӧ���������,����������ȫ���رգ����ǽ��ͷ�������Դ
 * @PARAM	check_type
 * @RETURN	void
 * @RETVAL	
 */

void anyka_dection_stop(int check_type)
{
    if(pvideo_move_ctrl == NULL)
    {
        return;
    }
    if(check_type & SYS_CHECK_VIDEO_ALARM)
    {
        //�ر��ƶ����
        if(pvideo_move_ctrl->check_type & SYS_CHECK_VIDEO_ALARM)
        {
            video_stop_move_check();
            pvideo_move_ctrl->check_type &= ~SYS_CHECK_VIDEO_ALARM;
            anyka_print("[%s:%d] It will stop video check!\n", __func__, __LINE__);
        }
    }
    if(check_type & SYS_CHECK_VOICE_ALARM)
    {
        //�ر��������
        if(pvideo_move_ctrl->check_type & SYS_CHECK_VOICE_ALARM)
        {
            audio_del(SYS_AUDIO_RAW_PCM, (void *)pvideo_move_ctrl);
            pvideo_move_ctrl->check_type &= ~SYS_CHECK_VOICE_ALARM;
            anyka_print("[%s:%d] It will stop voice check!\n", __func__, __LINE__);
        }
    }
	if(check_type & SYS_CHECK_OTHER_ALARM)
	{
	    if(pvideo_move_ctrl->check_type & SYS_CHECK_OTHER_ALARM)
        {
            //audio_del(SYS_AUDIO_RAW_PCM, (void *)pvideo_move_ctrl);
            pvideo_move_ctrl->check_type &= ~SYS_CHECK_OTHER_ALARM;
            anyka_print("[%s:%d] It will stop voice check!\n", __func__, __LINE__);
        }
	}
    if(pvideo_move_ctrl->check_type == 0)
    {
        //����������ȫ���رպ󣬽��ͷ���Դ
        pvideo_move_ctrl->run_flag = 0;
        if(pvideo_move_ctrl->save_record_flag)
        {
            video_del(pvideo_move_ctrl);
            audio_del(SYS_AUDIO_ENCODE_AMR, pvideo_move_ctrl);

            sem_post(&pvideo_move_ctrl->data_sem);
            sem_post(&pvideo_move_ctrl->save_sem);
            pthread_join(pvideo_move_ctrl->id, NULL);	
            sem_post(&pvideo_move_ctrl->manage_sem);	
            pthread_join(pvideo_move_ctrl->save_file_id, NULL);

            pthread_mutex_lock(&pvideo_move_ctrl->move_mutex);

            anyka_queue_destroy(pvideo_move_ctrl->video_queue, anyka_dection_video_move_queue_free);
            anyka_queue_destroy(pvideo_move_ctrl->audio_queue, anyka_dection_video_move_queue_free);
            sem_destroy(&pvideo_move_ctrl->data_sem);
            sem_destroy(&pvideo_move_ctrl->save_sem);
    		sem_destroy(&pvideo_move_ctrl->manage_sem);
            anyka_dection_remove_tmp_file();
        }

        anyka_pthread_mutex_destroy(&pvideo_move_ctrl->move_mutex); 
		
		anyka_dection_free_record_info();	

        free(pvideo_move_ctrl);
        pvideo_move_ctrl = NULL;
        anyka_print("[%s:%d] It will release check!\n", __func__, __LINE__);
    }
}



/**
 * NAME         anyka_dection_pass_video_check
 * @BRIEF	�Ƿ�������Ļص���Ŀǰֻ����Ѷƽ̨ʹ�ã��ڹۿ���Ƶʱ���������
 * @PARAM	
 * @RETURN	0 :�����߼��޸ģ�1:����⣬����Ĭ�Ϸ��������Ϣ 2: ����⣬Ҳ�����������Ϣ
 * @RETVAL	
 */
int anyka_dection_pass_video_check(void)
{
    int flag;

    if (pvideo_move_ctrl == NULL)
        return 2;
        
    if(pvideo_move_ctrl->filter_check)
    {
        flag = pvideo_move_ctrl->filter_check();

        if(flag)
        {
            if(pvideo_move_ctrl->move_ok == 0)
            {
                return 2;
            }
            else
            {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * NAME         anyka_dection_init
 * @BRIEF	ϵͳ����ʱ����ȡ�����ļ������ػ�ǰ�Ƿ�������⣬�������
                    ������Ӧ����,����⵽��Ҫ����Ϣ��ȥ��Ŀǰֻ֧�ִ��ã��������
                    ƽ̨���޸Ļص������Ϳ���
 * @PARAM	Palarm_func  ����������ʱ�Ļص�����
                    filter_check   ��ǰ�Ƿ�������ģ�Ŀǰ�����Ѷ������Ƶ�ۿ���ʱ�������
 * @RETURN	void
 * @RETVAL	
 */


void anyka_dection_init(PANYKA_SEND_ALARM_FUNC Palarm_func, PANYKA_FILTER_VIDEO_CHECK_FUNC filter_check)
{	
    Psystem_alarm_set_info alarm = anyka_sys_alarm_info();
	
	anyka_print("[%s] ####\n", __func__);

    if(alarm->motion_detection)
    {
        //�����ƶ����
        anyka_dection_start(alarm->motion_detection - 1, SYS_CHECK_VIDEO_ALARM, Palarm_func, filter_check);
    }
    if(alarm->opensound_detection)
    { 
        //�����������
        anyka_dection_start(alarm->opensound_detection - 1, SYS_CHECK_VOICE_ALARM, Palarm_func, filter_check);
    }
	if(alarm->other_detection){ 
		anyka_print("[%s:%d] other detection \n", __func__, __LINE__);
		anyka_dection_start(alarm->other_detection - 1, SYS_CHECK_OTHER_ALARM, Palarm_func, filter_check);   
    }
	
}

/**
 * NAME         anyka_dection_save_record
 * @BRIEF	���¼���Ƿ�ʼ
 * @PARAM	
 * @RETURN	1-->�������¼��; 0-->���¼��δ��ʼ
 * @RETVAL	
 */

int anyka_dection_save_record(void)
{
    return pvideo_move_ctrl != NULL;
}


/**
 * NAME         anyka_dection_pause_dection
 * @BRIEF	ֹͣ�ƶ����������
 * @PARAM	
 * @RETURN	
 * @RETVAL	
 */

void anyka_dection_pause_dection(void)
{
    if(pvideo_move_ctrl && pvideo_move_ctrl->check_flag)
    {
#ifdef STOP_DECT_RECORD   //֧����ͣ���¼��
        //stop audio&video codec
        video_del(pvideo_move_ctrl);
        audio_del(SYS_AUDIO_ENCODE_AMR, pvideo_move_ctrl);
        sem_post(&pvideo_move_ctrl->data_sem);
#endif
        pvideo_move_ctrl->check_flag = 0;
    }
}

/**
 * NAME         anyka_dection_resume_dection
 * @BRIEF	�ָ��ƶ����������
 * @PARAM	
 * @RETURN	
 * @RETVAL	
 */

void anyka_dection_resume_dection(void)
{
    if(pvideo_move_ctrl && pvideo_move_ctrl->check_flag == 0)
    {
        pvideo_move_ctrl->check_flag = 1;

#ifdef STOP_DECT_RECORD   //֧����ͣ���¼��
		/** add video get data **/
        video_add(anyka_dection_move_video_data, (void *)pvideo_move_ctrl, FRAMES_ENCODE_RECORD, 15);
        /** add audio get data **/
        audio_add(SYS_AUDIO_ENCODE_AMR,anyka_dection_video_move_audio_data,(void *)pvideo_move_ctrl);
#endif
    }
}


