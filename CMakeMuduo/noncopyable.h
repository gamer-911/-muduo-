#pragma once


//noncopyable���̳�֮��������������������������
//��������������޷����п�������͸�ֵ����
class noncopyable
{
public:
	noncopyable(const noncopyable&) = delete;
	noncopyable& operator=(const noncopyable&) = delete;
protected:
	noncopyable() = default;
	~noncopyable() = default;
};