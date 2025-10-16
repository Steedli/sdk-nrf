Overview
********
This sample is for nRF54H20.
This sample demostrates how to connect two peripherals via DPPI 
when the peripherals are crossing local domain and global domain.
The signal sequence is as follows:

Timer020 <------> DPPIC020 <------> CPURAD_IPCT <------> IPCT130 <------> DPPIC130 <------> GPIOTE130

With this sequence, the Timer020 CC[0] event will trigger the GPIOTE130 OUT task.

The application is running on Radio core, the Application core is in sleep mode.

Configuration
************
Radio Domain:

&dppic020 {
	/* For now either `sink-channels` or `source-channels` property
	   must be defined even for interconnection within single DPPI domain.
	   See "Software limitations in nRF Connect SDK" chapter in the
	   "nRF54H20 hardware events" presentation. */
	sink-channels = <2>;
	source-channels = <3>;
	status = "okay";
};

&cpurad_ipct {
    	/* Remove default channels assignment */
	/delete-property/ source-channel-links;
	/delete-property/ sink-channel-links;

   	/* Radio IPCT channel 7 -> Global IPCT channel 3 (T020 CC0 event -> GPIOTE130 OUT task) */
	source-channel-links = <7 NRF_DOMAIN_ID_GLOBALSLOW 3>;
    	/* Global IPCT channel 4 -> Radio IPCT channel 6 (T131 CC5 event to -> T020 START task) */
	// sink-channel-links = <6 NRF_DOMAIN_ID_GLOBALSLOW 4>;
	status = "okay";
};

Global Domain whare GPIOTE130 is located:

/* DPPIC where IPCT130 is located. */
&dppic130 {
	/* Remove default channels assignment */
	/delete-property/ owned-channels;
	/delete-property/ sink-channels;
	/delete-property/ source-channels;
	/delete-property/ nonsecure-channels;

	owned-channels = <3 4>;
    	/* 3 for gpiote130 */
	sink-channels = <3>;
    	/* not used*/
	source-channels = <4>;
	status = "okay";
};



Building and Running
********************

On nRF54H20DK, the LED2 will toggle every second.
