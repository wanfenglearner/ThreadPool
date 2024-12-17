#include "threadpool.h"


int sum(int a, int b)
{
	std::this_thread::sleep_for(std::chrono::seconds(2));
	int cnt = 0;
	for (int i = a; i <= b; ++i)
	{
		cnt += i;
	}
	return cnt;
}
int main()
{
	{
		ThreadPool pool;
		pool.setPoolMode(PoolMode::MODE_CACHED);
		pool.start(10);

		std::future<int> res1 = pool.submitTask(sum, 1, 100);
		std::future<int> res2 = pool.submitTask(sum, 1, 1000000);
		std::future<int> res3 = pool.submitTask(sum, 1000, 1000000);
		std::future<int> res4 = pool.submitTask(sum, 1000, 1000000);
		std::future<int> res5 = pool.submitTask(sum, 1000, 1000000);
	
		std::cout << "res1>>>" << res1.get() << "<<<res1" << std::endl;
		std::cout << "res2>>>" << res2.get() << "<<<res2" << std::endl;
		std::cout << "res3>>>" << res3.get() << "<<<res3" << std::endl;
		std::cout << "res4>>>" << res4.get() << "<<<res4" << std::endl;
		std::cout << "res5>>>" << res5.get() << "<<<res5" << std::endl;
		
		
	}
	//getchar();
	return 0;
}