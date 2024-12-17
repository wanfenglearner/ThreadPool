
#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_
#include <iostream>
#include <queue>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
const int THREAD_SIZE_THRESHHOLD = 1024;	// 默认线程数量的最大上限阈值
const int TASK_SIZE_THRESHHOLD = 10;	// 默认任务数量的最大上限阈值
const int THREAD_MAX_IDLE_TIME = 60;		// 线程的最大空闲时间

// 线程池的工作模式
enum class PoolMode
{
	MODE_FIXED,	// 静态开辟线程
	MODE_CACHED	// 动态开辟线程
};

// 线程类
class Thread
{
public:
	/* 这里接受从线程池里传来的线程执行函数, 方便线程函数操作线程池里的任务
	* 同时记录下每个线程的id和对应的线程这样容易进行删除
	*/
	using ThreadFunc = std::function<void(int)>;
	// 线程的构造函数
	Thread(ThreadFunc func);
	
	/*	每个线程只允许有一个对象, 因此把左值的拷贝构造和左值的赋值删除
	*	右值的拷贝构造和右值的赋值默认化
	*/
	Thread(const Thread&) = delete;
	Thread& operator=(const Thread&) = delete;
	Thread(Thread&&) = default;
	Thread& operator=(Thread&&) = default;

	// 线程的析构函数
	~Thread() = default;

	void start();		// 开启线程
	int getId();		// 得到每个线程对应的ID

private:
	static int generatedId_;	// 生产线程的ID
	int id_;	// 每个线程对应的ID
	ThreadFunc func_;		// 接收从线程池里传来的线程函数
};



// 线程池类
class ThreadPool
{
public:
	/*	线程池只允许有一个, 因此把左值的拷贝构造和左值的赋值删除
	*	右值的拷贝构造和右值的赋值默认化
	*/
	// 线程池构造函数
	ThreadPool();
	// 线程池析构函数
	~ThreadPool();

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	ThreadPool(ThreadPool&&) = default;
	ThreadPool& operator= (ThreadPool&&) = default;


	// 设置线程池的工作模式
	void setPoolMode(PoolMode mode);
	
	// 设置创建线程的最大线程阈值
	void setThreadSizeThreshHold(int thresh);

	// 开始线程池
	void start(int threadSize);

	// 向线程池提交任务
	/*
		因为规定提交的传参形式是 submitTask(函数名, 变量1, 变量2, ...)
		因此需要用模版的可变参和引用折叠(辨别传的是左值或者右值)
		有的线程还需要知道它的返回值, 用 packaged_task<函数类型>(函数) 进行包装
		用 future<函数返回值类型> 接受
		// 推导出模版函数的返回值类型用 decltype(函数的调用)
	*/
	template <typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		// 用 packaged_task<>() 进行包装
		using TaskType = decltype(func(args...));
		auto task = std::make_shared< std::packaged_task<TaskType()> >
			(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<TaskType> result = task->get_future();

		// 先获取锁
		std::unique_lock<std::mutex> lock(taskQueMTx_);

		std::cout << "---等待提交任务---" << std::endl;

		// 判断任务队列是否满, 对于提交的任务只能等待1s, 超过直接反正失败
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool { return taskQue_.size() < taskSizeMaxThreshHold_; }))
		{
			// 说明任务队列已经满了提交任务失败
			// 反正这个类型的空值
			std::cerr << "----提交任务失败" << std::endl;
			auto task = std::make_shared< std::packaged_task<TaskType()>>(
				[&]()->TaskType {return TaskType(); }
			);
			// 运行才能得到返回值结果
			(*task)();
			return task->get_future();
		}
		//任务队列没有满可以提交
		// 这里taskQue里存放的是void函数, 不知道进行包装的函数具体细节, 那么这里进行
		// 嵌套处理直接存的是这个task任务的运行过程
		std::cout << "提交任务成功" << std::endl;
		taskQue_.emplace([task]()->void { (*task)(); });
		// 线程中任务的数量增加
		++taskSize_;
		// 通知消费的线程可以消费了
		notEmpty_.notify_all();

		// 判断如果线程模式是cached模式, 那么需要根据当前任务的数量和线程的数量进行创建
		if (mode_ == PoolMode::MODE_CACHED)
		{
			if (idleThreadSize_ < taskSize_ && curThreadSize_ < threadSizeMaxThreshHold_)
			{
				// 可以创建线程
				std::cout << "创建了一个线程" << std::endl;
				// 通过函数绑定器 bind 把线程函数传递过去
				auto thread = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
				int id = thread->getId();
				// 把创建的线程添加到线程池里
				threads_.emplace(id, std::move(thread));
				++curThreadSize_;
				++idleThreadSize_;
				threads_[id]->start();	// 启动创建的当前线程
			}
		}

		return result;
	}
	
private:
	// 检查线程池的运行状态
	bool checkPoolState();

	// 线程的执行函数
	void threadFunc(int id);

private:
	int initThreadSize_;	// 初始化线程的数量
	std::atomic_int curThreadSize_;	// 当前线程的数量
	std::atomic_int idleThreadSize_;	// 空闲线程的数量
	int threadSizeMaxThreshHold_;		// 线程数量的最大阈值
	// 存储每个线程, 采用映射关系, 方便删除 线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;



	// 任务传递是函数类型, 因此里面用函数类型接受
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;		// 存储任务
	std::atomic_int taskSize_;		// 任务的数量
	int taskSizeMaxThreshHold_;		// 能够存储的最大任务阈值

	std::mutex taskQueMTx_;		// 任务的互斥锁,一次只允许一个线程拿到
	std::condition_variable notEmpty_;	// 条件变量, 任务队列是否为空
	std::condition_variable notFull_;	// 条件变量, 任务队列是否满
	std::condition_variable exitCond_;	// 条件变量, 线程池析构

	std::atomic_int isPoolRunning_;	// 线程池的运行状态
	PoolMode mode_;		// 线程池的工作模式
};



#endif
