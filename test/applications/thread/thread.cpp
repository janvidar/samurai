/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/thread/thread.h>

#define MAX_X 198
#define MAX_Y 651
#define MAX_Z 321

class MyThread : public Thread
{
	public:
		MyThread(int data_) : Thread(0), data(data_)
		{
		
		}
		
		~MyThread()
		{
		
		}
		
		void run();
		
	private:
		int data;
};

void MyThread::run()
{
	printf("Started thread\n");
	int v = 0;
	for (int x = 0; x < MAX_X; x++)
	{
		for (int y = 0; y < MAX_Y; y++)
		{
			for (int z = 0; z < MAX_Z; z++)
			{
				for (int w = 0; w < data; w++)
					// if (z < x && y > z && v % z > y % x)
						v++;
			}
		}
	}
	
	printf("Ended thread\n");
}


int main(int , char** ) {

	MyThread a(50);
	MyThread b(10);
	a.start();
	b.start();
	
	a.wait();
	b.wait();
}
