# Field Stat 2简介

Field Stat是一个运行状态输出与统计库。

## 本地显示风格(style)

​	本地显示会输出到屏幕或文件，可选择输出瞬时值 `FS_CALC_SPEED`或累计值`FS_CALC_CURRENT`。

​	允许在`FS_start`启动后注册非Column的统计域。

### 域（Field ）

​	`FS_STYLE_FIELD` 同时输出累计值(sum)和瞬时值(speed/s，1秒内的变化值)

```
	  field_00	  field_01
sum	         0	    184300
speed/s	     0	     92150
```

​	函数接口 

```c
field_ids[i]=FS_register(handle, FS_STYLE_FIELD, FS_CALC_CURRENT, "field_01");
```

### 状态（Status）

​	`FS_STYLE_STATUS` 输出累计值或瞬时值

```c
status_00:  100  status_01: 10
```

​	函数接口 

```
status_ids[i]=FS_register(handle, FS_STYLE_STATUS, FS_CALC_CURRENT, "status_01");
```

### 表格 （Line&Column）

 `FS_STYLE_LINE`和`FS_STYLE_COLUMN `输出累计值或瞬时值。

```
___________________________________________________________________________________
	  	  	 column_0	  column_1	  column_3	  column_4	  column_5	      c0/c1
line_0       2.76e+04	  5.53e+04	  1.11e+05	  1.38e+05	  1.66e+05	   5.00e-01	
line_1       2.76e+04	  5.53e+04	  1.11e+05	  1.38e+05	  1.66e+05	   5.00e-01	
line_2       2.76e+04	  5.53e+04	  1.11e+05	  1.38e+05	  1.66e+05	   5.00e-01	
line_3       2.76e+04	  5.53e+04	  1.11e+05	  1.38e+05	  1.66e+05	   5.00e-01	
line_4       2.76e+04	  5.53e+04	  1.11e+05	  1.38e+05	  1.66e+05	   5.00e-01	
line_5       2.76e+04	  5.53e+04	  1.11e+05	  1.38e+05	  1.66e+05	   5.00e-01	
line_6       2.76e+04	  5.53e+04	  1.11e+05	  1.38e+05	  1.66e+05	   5.00e-01
```

​	函数接口 API

```
line_ids[i]=FS_register(handle, FS_STYLE_LINE, FS_CALC_CURRENT, "line_0");
column_ids[i]=FS_register(handle, FS_STYLE_COLUMN, FS_CALC_SPEED,"column_0");
```



### 分布图 (Histogram)

​	`FS_STYLE_HISTOGRAM` 输出事件的数值分布，如延迟，文件长度等。

```
histogram	 50%   80%   95%   MAX   MIN    AVG   STDDEV   CNT
ssl_up(ms)	   0     0     0     0     0    0.00    0.00     0
peek_sni(ms)   0     0     0     0     0    0.00    0.00     0
ssl_down(ms)   0     0     0     0     0    0.00    0.00     0
ask_kyr(ms)	   0     0     0     0     0    0.00    0.00     0
```

​	默认输出 50%，80.00%，90%，95%和99%的比例， 可以通过 `FS_set_para` 函数的`HISTOGRAM_GLOBAL_BINS`选项修改。

​	缩写定义：

- MAX 最大值

- MIN 最小值

- AVG 平均值

- STDDEV 标准差

- CNT 计数

  函数接口 

```
fs_metric_sql_exec=FS_register_histogram(g_info.fs_handle,	//Field Stat句柄
										FS_CALC_CURRENT,	//输出累计值或瞬时值
                                        "sql_exec(us)",		//统计项名称，字符串
                                        1,					//可追踪的最小值
                                        1000000,			//可追踪的最大值
                                        2);					//精度，即小数点后几位，范围1~4
```



## 远程输出

​	可以输出为[StatsD](https://github.com/influxdata/telegraf/tree/master/plugins/inputs/statsd)格式或 [InfluxDB Inline Protocol](https://docs.influxdata.com/influxdb/v1.7/write_protocols/line_protocol_tutorial/)格式，通过UDP协议发送给聚合器，如telegraf。	聚合器接收并聚合后，写入时序数据库，如InfluxDB，然后通过图形界面展示，如Grafana。

StatsD 输出counter和histogram两种metric，metric name命名遵循

- Field和Status： ` [APP_NAME]field_name:value` 
- Column和Line：`[APP_NAME]line#column:value`

Influx Line命名规则为

```
measurement, tag, key1=value1, key2=value2
line，app_name=APPNAME column1=value,column2=value
```

​	可以通过 `FS_set_para`函数 的`STATS_SERVER_IP ` 、`STATS_SERVER_PORT`选项，设置服务器的IP和端口。通过`STATS_FORMAT`选项设置输出格式为`FS_OUTPUT_STATSD`或`FS_OUTPUT_INFLUX_LINE`。

​	服务端搭建流程参见[《【StatsD监控】基于Telegraf+InfluxDB+grafana展示（非Docker方案）》](https://blog.csdn.net/littlefang/article/details/78473161)。聚合Histogram建议修改telegraf.conf的以下配置项：

`percentiles = [50, 60, 80, 90, 95, 99]`

## 统计

### 记录统计值

​	通过`FS_operate`函数操作统计域，函数内部使用原子变量，无锁且线程安全。

```
//handle: FieldStat句柄
//id: FS_register_**返回的ID
//column_id: 列ID，非column类型填0
//filed_op: FS_OP_ADD: + value
//			FS_OP_SUB: - value
//			FS_OP_SET: = value
int FS_operate(screen_stat_handle_t handle,int id,int column_id,enum field_op op,long long value);
```

### 记录事件耗时

​	应使用clock_gettime函数获得事件的耗时，[永远不要用gettimeofday计算时间](https://blog.habets.se/2010/09/gettimeofday-should-never-be-used-to-measure-time.html)。

​	也可使用field_stat2.h中提供的简单封装。

```
struct timespec start;
record_time_start(&start);
//do something
long eplapses=record_time_elapse_us(&start);
FS_operate(fs_handle, histogram_id, 0, FS_OP_SET, eplapsed);
```



## 使用建议

### 命名建议

​	在命名时，遵循按照以下规则：

1. 带上单位，如bytes或kb，微秒us或毫秒ms等。
2. 除若干单词首字母组成的缩写外，不要大写。如Kernel Network Interface 可以缩写为KNI，packet缩写为pkt，而不是PKT
3.  一般不要超过10个字节，不然本地刷新不美观。
   - htable->htab, read->rd，write->wr，send ->tx，receive->rx

   - send packets，可缩写为tx_pkts，send bytes可缩写为tx_bytes

     禁止注册名称包含 `|:\n\r. \t<>[]#!@`的统计域。

### 显示字段数量

​	Status和Field每行固定展示8个统计值。Column和Histogram取决于注册数量，建议Column一般不超过8个，Histogram的bins不超过5个。

​	通过`FS_set_para`函数的 `ID_INVISBLE`不显示的字段，可以节约屏幕空间；通过 `FS_register_ratio`计算两个统计字段的比值。

### 本地结果查看

​	在shell执行`watch -d cat 日志文件`，观察实时的输出结果。

