#ifndef SINGLETON_HPP
#define SINGLETON_HPP

template<typename DataType, int instanceId = 0>
class LeakSingleton 
{
public: 
	static DataType* instance()
	{
		return dataM;
	}
	
	static void init()
	{
		dataM = new DataType;		
	}
private:
	LeakSingleton(){};
	~LeakSingleton(){};

	static DataType* dataM;
};

template<typename DataType, int instanceId>
DataType* LeakSingleton<DataType, instanceId>::dataM = NULL;

#endif /* SINGLETON_HPP */

