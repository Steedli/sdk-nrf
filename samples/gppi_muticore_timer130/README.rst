Overview
********
This sample is for nRF54H20.
This sample demostrates how to connect two peripherals via DPPI 
when the peripherals are in different global domain.
The signal sequence is as follows:

Timer137 <------> DPPIC136 <------> DPPIC130 <------> GPIOTE130

With this sequence, the Timer137 CC[0] event will trigger the GPIOTE130 OUT task.

The application is running on Radio core, the Application core is in sleep mode.

Configuration
************
Global Domain where Timer137 is located:

/* DPPIC where TIMER137 is located. */
&dppic136 {
	/* Remove default channels assignment */
	/delete-property/ owned-channels;
	/delete-property/ sink-channels;
	/delete-property/ source-channels;
	/delete-property/ nonsecure-channels;

	owned-channels = <3 4>;
    	/* not used*/
	sink-channels = <4>;
    	/* Timer137 cc[0] event*/
	source-channels = <3>;
	status = "okay";
};

Global Domain whare GPIOTE130 is located:

/* DPPIC where GPIOTE130 is located. */
&dppic130 {
	/* Remove default channels assignment */
	/delete-property/ owned-channels;
	/delete-property/ sink-channels;
	/delete-property/ source-channels;
	/delete-property/ nonsecure-channels;

	owned-channels = <3 4>;
    	/* gpiote130 output task*/
	sink-channels = <3>;
    	/* not used*/
	source-channels = <4>;
	status = "okay";
};



Building and Running
********************

On nRF54H20DK, the LED2 will toggle every second.
