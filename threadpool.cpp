
#include "threadpool.h"



/////////////////////////////线程池的设置

// 线程池构造函数
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,curThreadSize_(0)
	,idleThreadSize_(0)
	,threadSizeMaxThreshHold_(THREAD_SIZE_THRESHHOLD)
	,taskSize_(0)
	,taskSizeMaxThreshHold_(TASK_SIZE_THRESHHOLD)
	,isPoolRunning_(false)
{
}

// 线程池析构函数
ThreadPool::~ThreadPool()
{
	// 线程池析构时如果此时任务队列有任务还没有执行晚 
	// 应该阻塞等待任务执行完
	isPoolRunning_ = false;

	// 这里为了避免死锁, 需要进行先获得锁在进行通知
	std::unique_lock<std::mutex> lock(taskQueMTx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });

	
}

// 设置线程池的工作模式
void ThreadPool::setPoolMode(PoolMode mode)
{
	// 已经运行就不准设置了
	if (checkPoolState())
	{
		return;
	}
	mode_ = mode;
}
// 设置创建线程的最大线程阈值
void ThreadPool::setThreadSizeThreshHold(int thresh)
{
	// 已经运行就不准设置了
	if (checkPoolState())
	{
		return;
	}
	threadSizeMaxThreshHold_ = thresh;
}
// 开始线程池
void ThreadPool::start(int threadSize)
{
	// 线程池启动
	isPoolRunning_ = true;
	// 记录初始线程的个数
	initThreadSize_ = threadSize;
	curThreadSize_ = threadSize;
	
	// 创建线程
	for (int i = 0; i < initThreadSize_; ++i)
	{
		// 通过函数绑定器 bind 把线程函数传递过去
		auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int id = thread->getId();
		// 把创建的线程添加到线程池里
		threads_.emplace(id, std::move(thread));
	}
	// 启动所有创建的线程
	for (int i = 0; i < initThreadSize_; ++i)
	{
		threads_[i]->start();
		// 空闲的线程数量增加
		++idleThreadSize_;
	}

}


// 检查线程池的运行状态
bool ThreadPool::checkPoolState()
{
	return isPoolRunning_;
}


//////////////////////////////////////线程的设置
// 静态成员变量类内声明类外初始化
int Thread::generatedId_ = 0;
// 线程的构造函数
Thread::Thread(ThreadFunc func)
	:func_(func)
	,id_(generatedId_++)
{}
// 开启线程
void Thread::start()
{
	// 创建线程
	std::thread t(func_, id_);
	// 设置分离线程
	t.detach();
}
// 得到每个线程对应的ID
int Thread::getId()
{
	return id_;
}

// 线程的执行函数
void ThreadPool::threadFunc(int id)
{
	// 记录下当前线程的上一次的运行的时刻
	auto lastTime = std::chrono::high_resolution_clock().now();
	// 每个线程都不能停,循环的去检查任务队列里是否有任务, 如果有就抢到锁之后开始执行
	while (1)
	{
		// 用来接受task对象的
		Task task;

		{
			// 先拿锁
			std::unique_lock<std::mutex> lock(taskQueMTx_);
			std::cout << "---等待拿数据---" << std::endl;
			// 判断当前任务队列里是否还有数据
			while (taskQue_.size() == 0)
			{
				// 这里为了防止死锁, 如果线程池都已经不运行了, 那线程也没必要运行了
				if (!isPoolRunning_)
				{
					--curThreadSize_;
					--idleThreadSize_;
					threads_.erase(id);
					// 告知判断是否可以线程池析构
					exitCond_.notify_all();
					std::cout << "threadId:" << std::this_thread::get_id() << "exit" << std::endl;
					return;
				}
				// 动态模式
				if (mode_ == PoolMode::MODE_CACHED)
				{
					/* 这里就会有个问题, 当任务队列里没有数据的时候,
					* 如果创建了很多线程,理应当释放多余的线程
					* 这里可以进行判断如果这个线程超过了60s都还是处于空闲状态那么可以
					* 直接回收了
					*/

					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						// 超时时间返回的即为没有任务
						auto nowTime = std::chrono::high_resolution_clock().now();
						// 计算 nowTime和lastTime二者相差的时间
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);
						// 如果超过了 THREAD_MAX_IDLE_TIME 回收多余的线程
						if (dur.count() > THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// 进行线程回收
							--curThreadSize_;
							--idleThreadSize_;
							// 在线程列表里删除
							threads_.erase(id);
							std::cout << "threadId:" << std::this_thread::get_id() << "exit" << std::endl;
							return;
						}
					}

				}
				else   //静态模式 
				{
					notEmpty_.wait(lock);
				}
			}
			// 空闲的线程数量减少
			--idleThreadSize_;
			// 有数据可以进行拿
			std::cout << "threadId:" << std::this_thread::get_id() << "拿数据成功" << std::endl;
			task = taskQue_.front();
			taskQue_.pop();
			--taskSize_;
			// 这里进行判断当前线程如果拿了一个任务之后还有任务, 就可以给其他消费线程
			// 通知无需再等待了
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}
			// 对生产的线程进行通知可以进行生产了
			notFull_.notify_all();
		}	// 这里需要进行释放锁, 因为已经拿到任务了, 应该把锁释放掉方便其他线程拿

		if (task != nullptr)
		{
			// 执行任务
			task();
		}
		// 当前线程执行完任务了,处于空闲状态
		++idleThreadSize_;
		// 更新当前线程处理完这个任务的时刻
		lastTime = std::chrono::high_resolution_clock().now();
	}


}