#ifndef RPLIDAR_A2_LIDAR_HPP
#define RPLIDAR_A2_LIDAR_HPP

#include "SerialCommunication.hpp"
#include "ReturnDataWrappers.hpp"
#include "FullScan.hpp"
#include <sys/time.h>
#include <array>


class RPLidar {

	SerialCommunication port;
	FullScan current_scan;
	bool scan_on=false;
	bool motor_on=false;
	int8_t check_new_turn(float next_angle, FullScan &current_scan);
	bool process_express_scans();
	rp_values::ComResult set_pwm(uint16_t pwm);
	rp_values::ComResult send_packet(rp_values::OrderByte order, const std::vector<uint8_t> &payload={});
    rp_values::ComResult read_scan_data(std::vector<uint8_t> &output_data, uint8_t size, bool to_sync = false);
    rp_values::ComResult start_motor();
    rp_values::ComResult stop_motor();

public:
	rp_values::ComResult get_health(data_wrappers::HealthData *health_data);
	rp_values::ComResult get_info(data_wrappers::InfoData *info_data);
	rp_values::ComResult get_samplerate(data_wrappers::SampleRateData *sample_rate);
	rp_values::ComResult stop_scan();
	void print_status();
	void print_scan();
	void print_deltas();
	bool stop();

	//Interface for fusion_lidars:
	RPLidar()= default;
	~RPLidar();
	bool init(const char *serial_path);
	bool start() ;
	void update() ;
	const std::vector<std::pair<float, uint16_t>>* getDataPoints() const;
	bool close() ;
};

#endif //RPLIDAR_A2_LIDAR_HPP