//该文件为自创文件，无pintos源码，因此不再使用中文注释等作为更改标注
//UNLESS IT'S TO_INT ,ALL METHODS RETURN FP TYPE OF INT
//fp、int混合操作函数，开头a均为fp

#define Q_SHIFT_LEFT 14
#define AID_Q 1<<14
typedef int fp;


#define INT_TO_FP(A) ((fp)((A)<<Q_SHIFT_LEFT))

#define FP_TO_INT_ZERO(a) ((int)((a)>>Q_SHIFT_LEFT))

//	(x + f / 2) / f如果x >= 0，
//(x - f / 2) / f如果x <= 0。
#define FP_TO_INT_NEAREST(a) (a > 0 ? (a + AID_Q/2)/AID_Q : (a - AID_Q/2)/AID_Q)

#define ADD_FP_FP(a,b) ((a) + (b))

#define ADD_FP_INT(a,b) ((a) + (b) * AID_Q)

#define SUB_FP_FP(a,b) ((a) - (b))

#define SUB_FP_INT(a,b) ((a) - (b) * AID_Q)

#define MULTI_FP(A,B)

#define MULTI_FP_INT(A,B)

#define DIVIDE_FP(a,b)

#define DIVIDE_FP_INT(A,B)