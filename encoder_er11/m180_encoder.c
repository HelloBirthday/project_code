#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>

#define GPIO_HIGH           1
#define GPIO_LOW            0
#define ENCODER_HIGH        GPIO_HIGH
#define ENCODER_LOW         GPIO_LOW

/*input report adc value  of knobs*/
#define ENCODER_KEYUP		KEY_GINTA   
#define ENCODER_KEYDOWN 	KEY_GINTB

/*value of encoder physics key*/
#define ENCODER_OK 		    KEY_DOU


/*name of input device encoder */
#define ENCODER_NAME   		"encoder"

struct  encoder_data{
        struct device *dev;   
        unsigned int encoder_irqa;    //signals A  irq
        unsigned int encoder_irqb;    //signals  B  irq
	    unsigned int encoder_key;
	    unsigned int count;   //pulses number
	    struct input_dev *input;
};

struct gpio_config_info {
        unsigned int gpio_a;    //a   gpio
        unsigned int gpio_b;    //b  gpio
	    unsigned int gpio_key;
        unsigned int irqa;
    	unsigned int irqb;
	    unsigned int irqkey;
};

static struct gpio_config_info *this_gpio;
static struct encoder_data *this;


static void  encoder_report_value(int flags)
{

    if(ENCODER_HIGH == flags){
		input_event(this->input, EV_KEY, ENCODER_KEYUP, 1);  
		input_sync(this->input);	
		udelay(10);
		input_event(this->input, EV_KEY, ENCODER_KEYUP, 0);  
		input_sync(this->input);	
        
    }
    else{
		input_event(this->input, EV_KEY, ENCODER_KEYDOWN, 1);  
		input_sync(this->input);	
		udelay(10);
		input_event(this->input, EV_KEY, ENCODER_KEYDOWN, 0);  
		input_sync(this->input);	
    
    }

}



static irqreturn_t gpio_key_interrupt(int irq,void *data)
{
        disable_irq_nosync(irq);
	
	input_event(this->input, EV_KEY, ENCODER_OK, 1);  //249
	input_sync(this->input);	
	udelay(10);
	input_event(this->input, EV_KEY, ENCODER_OK, 0);  
	input_sync(this->input);	
        enable_irq(irq);
	
        return IRQ_HANDLED;
}


static irqreturn_t gpioa_interrupt(int irq,void *data)
{
        int value1;
        int value2;


        disable_irq_nosync(irq);
        value1 = gpio_get_value(this_gpio->gpio_a);
        value2 = gpio_get_value(this_gpio->gpio_b);                                                                                

        irq_set_irq_type(irq,value1==GPIO_HIGH?IRQF_TRIGGER_LOW:IRQF_TRIGGER_HIGH);
        if(value1 == GPIO_HIGH)
        {
                if(value2 == GPIO_LOW){
                        this->count++;
		                encoder_report_value(ENCODER_HIGH);
		        }
                else{
                        this->count--;
		                encoder_report_value(ENCODER_LOW);
		        }
                //printk( " A count:%d\n",pdata->count);    //count 为奇数，
		/*encoder report value*/
        }
        enable_irq(irq);

        return IRQ_HANDLED;
}


static irqreturn_t gpiob_interrupt(int irq,void *data)
{
        int value1;
        int value2;

        disable_irq_nosync(irq);

        value1 = gpio_get_value(this_gpio->gpio_a);
        value2 = gpio_get_value(this_gpio->gpio_b);
        irq_set_irq_type(irq,value2==GPIO_HIGH?IRQF_TRIGGER_LOW:IRQF_TRIGGER_HIGH);
        if(value2 == GPIO_LOW)
        {
                if(value1 == GPIO_LOW){
                        this->count++;
		                encoder_report_value(ENCODER_HIGH);
		        }
                else{
                        this->count--;
		                encoder_report_value(ENCODER_LOW);
		        }


                //printk( "B count:%d\n",pdata->count);
        }
                                                                                                                                   
        enable_irq(irq);


        return IRQ_HANDLED;
}


int input_chip_init(struct encoder_data *encoder)
{
	
	int ret = 0;
	struct input_dev *input_dev;

        input_dev = input_allocate_device();
        if(!input_dev){
                printk("encoder alloc input device failed\n");
                return -1; 
        }
       	 
        input_dev->name = ENCODER_NAME;
        input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0002;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = this->dev;
	this->input = input_dev;
 
 	// 方式1:
 	input_dev->evbit[0] = BIT_MASK(EV_KEY);
        input_dev->keybit[BIT_WORD(BTN_0)] = BIT_MASK(BTN_0);
 	// 方式2:
	/*
	set_bit(EV_KEY,input_dev->evbit);
	set_bot(BTN_0.input_dev->keybit);
	*/

 	/*该函数记录感兴趣的事件：EV_KEY,并且指定了其code：ENCODER_KEYUP、ENCODER_KEYDOWN,其他按键不予理睬*/	
 	input_set_capability(input_dev, EV_KEY, ENCODER_KEYUP);
	input_set_capability(input_dev, EV_KEY, ENCODER_KEYDOWN);


	ret = input_register_device(input_dev);
	if(ret){
		printk("register input device encoder re11 error.\n");	
		return -1;
	}

	return 0;	
}


static int encoder_probe(struct platform_device *pdev)
{


        struct encoder_data *encoder;
        struct gpio_config_info *gpio_cfg;
        enum of_gpio_flags flags1,flags2,flags3;
        struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
        int err = 0;
 
        encoder = kzalloc(sizeof(struct encoder_data),GFP_KERNEL);
        if(!encoder){
                dev_err(&pdev->dev,"encoder kzalloc failed..\n");
                return -EINVAL;
        }
 
        gpio_cfg = kzalloc(sizeof(struct gpio_config_info),GFP_KERNEL);
        if(!gpio_cfg){
                dev_err(&pdev->dev,"gpio_cfg kzalloc failed..\n");
                return -EINVAL;
        }
        encoder->count = 0;
        this_gpio = gpio_cfg;
	this = encoder;
	encoder->dev = &pdev->dev;
 
 /*==============================================GPIO_A============================================*/
        gpio_cfg->gpio_a = of_get_named_gpio_flags(node,"gpio_a",0,&flags1);                                                       
        if(!gpio_is_valid(gpio_cfg->gpio_a)){
                printk("get a gpiofailed.\n");
        }
        else{
                err = devm_gpio_request(dev,gpio_cfg->gpio_a,"encoder-gpio");
                if(err < 0){
                        printk("gpio:devm_gpio_request failed  %d\n",err);      
                }
 
                gpio_direction_output(gpio_cfg->gpio_a, (flags1 & OF_GPIO_ACTIVE_LOW)!=0?0:1);
                gpio_set_value(gpio_cfg->gpio_a,(flags1 & OF_GPIO_ACTIVE_LOW)!=0?0:1);
                mdelay(10);
 
 
                //set gpio_a interrupt mode
                encoder->encoder_irqa = gpio_to_irq(gpio_cfg->gpio_a);
                if(encoder->encoder_irqa < 0){
                        err = encoder->encoder_irqa;
                        pr_err("gpio-: Unable to get irqa number for GPIO A, error %d\n", err);
                        gpio_free(gpio_cfg->gpio_a);
                }

	}
        
 /*=======================================================================================================*/
 
 
 
 /*==============================================GPIO_B============================================*/
        gpio_cfg->gpio_b = of_get_named_gpio_flags(node,"gpio_b",0,&flags2);
        if(!gpio_is_valid(gpio_cfg->gpio_b)){
                printk("get a gpiofailed.\n");
        }
        else{
                err = devm_gpio_request(dev,gpio_cfg->gpio_b,"encoder-gpio");
                if(err < 0){
                        printk("gpio:devm_gpio_request failed  %d\n",err);                                                         
                }
 
                gpio_direction_output(gpio_cfg->gpio_b, (flags2 & OF_GPIO_ACTIVE_LOW)!=0?0:1);
                gpio_set_value(gpio_cfg->gpio_b,(flags2 & OF_GPIO_ACTIVE_LOW)!=0?0:1);
                mdelay(10);
 
 
                //set gpio_b interrupt mode
                encoder->encoder_irqb = gpio_to_irq(gpio_cfg->gpio_b);
                if(encoder->encoder_irqb < 0){
                        err = encoder->encoder_irqb;
                        pr_err("gpio-: Unable to get irqa number for GPIO A, error %d\n", err);
                        gpio_free(gpio_cfg->gpio_b);

                }
                
        }
/*=====================================================================================================*/


 /*==============================================GPIO_KEY============================================*/
        gpio_cfg->gpio_key = of_get_named_gpio_flags(node,"gpio_key",0,&flags3);
        if(!gpio_is_valid(gpio_cfg->gpio_key)){
                printk("get a gpio_key failed.\n");
        }
        else{
                err = devm_gpio_request(dev,gpio_cfg->gpio_key,"encoder-gpio");
                if(err < 0){
                        printk("gpio:devm_gpio_key_request failed  %d\n",err);                                                         
                }
 
                gpio_direction_output(gpio_cfg->gpio_key, (flags3 & OF_GPIO_ACTIVE_LOW)!=0?0:1);
                gpio_set_value(gpio_cfg->gpio_key,(flags3 & OF_GPIO_ACTIVE_LOW)!=0?0:1);
                mdelay(10);
 
 
                //set gpio_b interrupt mode
                encoder->encoder_key = gpio_to_irq(gpio_cfg->gpio_key);
                if(encoder->encoder_key < 0){
                        err = encoder->encoder_key;
                        pr_err("gpio-: Unable to get irqkey number for GPIO key, error %d\n", err);
                        gpio_free(gpio_cfg->gpio_key);

                }
                
        }
/*==============================================END=======================================================*/



	err = input_chip_init(encoder); 
 	if(err < 0){
		printk("encoder input chip device failed.\n");	
		goto failed;
	}

 
 
 //request gpio interrupt
        
        if(gpio_cfg->gpio_a != -1){
                err = request_irq(encoder->encoder_irqa,gpioa_interrupt,gpio_get_value(gpio_cfg->gpio_a)==GPIO_LOW?IRQF_TRIGGER_HIGH:IRQF_TRIGGER_LOW, "encoder-gpio1", encoder);                
                if(err){
		   printk("gpioa request irq failed \n");	 
                 	//goto failed;
                }
        }
 
 
        if(gpio_cfg->gpio_b != -1){
                err = request_irq(encoder->encoder_irqb,gpiob_interrupt, gpio_get_value(gpio_cfg->gpio_b)==GPIO_LOW?IRQF_TRIGGER_HIGH:IRQF_TRIGGER_LOW, "encoder-gpio2", encoder);
                if(err){
                        printk("gpiob request irq failed \n");
                        //goto failed;
                }
        }


        if(gpio_cfg->gpio_key != -1){
                err = request_irq(encoder->encoder_key,gpio_key_interrupt, IRQF_TRIGGER_FALLING, "encoder-gpio3", encoder);
                if(err){
                        printk("gpiob request irq failed \n");
                        //goto failed;
                }
        }



        platform_set_drvdata(pdev,encoder);


        printk("rk3288  encoder re11 probe success.\n");


	
failed:
	printk("error\n");	


	return 0;
        

}



static int  encoder_remove(struct platform_device *pdev)
{
        return 0;
}
                                                                                                                                   
static struct of_device_id encoderid[] = {
        { .compatible = "rk3288,encoder" },
        { /* Sentinel */},
};
MODULE_DEVICE_TABLE(of,encoderid);

static struct platform_driver encoder_driver = {
	.probe  = encoder_probe,
	.remove = encoder_remove,
        .driver         = {
                .name   = "encoder",
                .owner  = THIS_MODULE,
                .of_match_table = of_match_ptr(encoderid),
        },
};


module_platform_driver(encoder_driver);
MODULE_LICENSE("GPL");


