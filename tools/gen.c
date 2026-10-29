/* tools/gen.c - Synthetic data generator for GateDB (C11, standard library only)
 * Generates realistic hostel database CSVs to demonstrate index efficiency.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static uint64_t rng_state = 88172645463325252ULL;
static uint32_t xorshift32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)rng_state;
}
static uint32_t rand_range(uint32_t min_val, uint32_t max_val) {
    if (min_val >= max_val) return min_val;
    return min_val + (xorshift32() % (max_val - min_val + 1));
}

static const char *FIRST_NAMES[] = {
    "Darshana", "Shivani", "Aarohi", "Meera", "Ishita", "Shreya", "Neha", "Pooja",
    "Sneha", "Tanvi", "Riya", "Ananya", "Pranali", "Vaishnavi", "Komal", "Sakshi",
    "Aarav", "Aditya", "Rohan", "Rahul", "Shubham", "Sahil", "Varun", "Yash",
    "Rohit", "Vikram", "Siddharth", "Amit", "Kunal", "Gaurav", "Tejas", "Swapnil",
    "Omkar", "Prathamesh", "Abhishek", "Nikhil", "Chinmay", "Harsh", "Sanket", "Mayur"
};
static const size_t NUM_FIRST = sizeof FIRST_NAMES / sizeof *FIRST_NAMES;

static const char *LAST_NAMES[] = {
    "Kulkarni", "Mahajan", "Patil", "Joshi", "Deshmukh", "Shinde", "Pawar", "Gaikwad",
    "More", "Chavan", "Jadhav", "Tambe", "Salunkhe", "Wagh", "Kale", "Jagtap",
    "Bhosale", "Kadam", "Sawant", "Gokhale", "Bapat", "Apte", "Kamat", "Naik"
};
static const size_t NUM_LAST = sizeof LAST_NAMES / sizeof *LAST_NAMES;

static const char *CLASSES[] = {
    "FY CSE", "SY CSE", "TY CSE", "LY CSE",
    "FY IT", "SY IT", "TY IT", "LY IT",
    "FY ENTC", "SY ENTC", "TY ENTC", "LY ENTC",
    "FY Mech", "SY Mech", "TY Mech", "LY Mech",
    "FY Civil", "SY Civil", "TY Civil", "LY Civil"
};
static const size_t NUM_CLASSES = sizeof CLASSES / sizeof *CLASSES;

static const char *CITIES[] = {
    "Pune, MH", "Mumbai, MH", "Nashik, MH", "Nagpur, MH", "Satara, MH",
    "Kolhapur, MH", "Solapur, MH", "Aurangabad, MH", "Amravati, MH", "Thane, MH"
};
static const size_t NUM_CITIES = sizeof CITIES / sizeof *CITIES;

int main(int argc, char **argv) {
    const char *out_dir = "data/demo";
    int num_students = 2500;
    int num_logs = 15000;

    if (argc > 1) out_dir = argv[1];
    if (argc > 2) num_students = atoi(argv[2]);
    if (argc > 3) num_logs = atoi(argv[3]);

    char path[512];

    /* 1. hostel.csv */
    snprintf(path, sizeof path, "%s/hostel.csv", out_dir);
    FILE *fh = fopen(path, "w");
    if (!fh) { perror(path); return 1; }
    fputs("id,name,address,latitude,longitude,geofence_radius_m,gate_closure_time\n", fh);
    fputs("1,Girls Hostel (Demo),\"College of Engineering Pune, Shivajinagar\",18.5293,73.8566,150,21:00:00\n", fh);
    fputs("2,Boys Hostel Block A,\"College of Engineering Pune, Shivajinagar\",18.5290,73.8560,150,21:30:00\n", fh);
    fputs("3,Boys Hostel Block B,\"College of Engineering Pune, Shivajinagar\",18.5295,73.8562,150,21:30:00\n", fh);
    fputs("4,Girls Hostel Block B,\"College of Engineering Pune, Shivajinagar\",18.5298,73.8570,150,21:00:00\n", fh);
    fputs("5,PG / Research Hostel,\"College of Engineering Pune, Shivajinagar\",18.5285,73.8555,150,22:00:00\n", fh);
    fclose(fh);

    /* 2. in_out_time.csv */
    snprintf(path, sizeof path, "%s/in_out_time.csv", out_dir);
    FILE *fio = fopen(path, "w");
    if (!fio) { perror(path); return 1; }
    fputs("id,hostel_id,out_time,in_time\n", fio);
    fputs("1,1,06:00:00,20:30:00\n", fio);
    fputs("2,2,06:00:00,21:00:00\n", fio);
    fputs("3,3,06:00:00,21:00:00\n", fio);
    fputs("4,4,06:00:00,20:30:00\n", fio);
    fputs("5,5,05:30:00,21:30:00\n", fio);
    fclose(fio);

    /* 3. warden.csv */
    snprintf(path, sizeof path, "%s/warden.csv", out_dir);
    FILE *fw = fopen(path, "w");
    if (!fw) { perror(path); return 1; }
    fputs("id,username,name,mobile_no,password_hash,hostel_id\n", fw);
    fputs("1,warden1,Mrs. Kulkarni (Warden),\"{9888800001}\",x,1\n", fw);
    fputs("2,warden2,Mr. Shinde (Warden),\"{9888800002}\",x,2\n", fw);
    fputs("3,warden3,Mr. Patil (Warden),\"{9888800003}\",x,3\n", fw);
    fputs("4,warden4,Mrs. Joshi (Warden),\"{9888800004}\",x,4\n", fw);
    fputs("5,warden5,Dr. Deshmukh (Warden),\"{9888800005}\",x,5\n", fw);
    fclose(fw);

    /* 4. student.csv */
    snprintf(path, sizeof path, "%s/student.csv", out_dir);
    FILE *fs = fopen(path, "w");
    if (!fs) { perror(path); return 1; }
    fputs("mis,name,class,room,home_address,admission_date,password_hash,phone_no,parents_phone_no,hostel_id,created_at\n", fs);

    /* Original demo students first */
    fputs("612415090,Darshana Kulkarni,TY CSE,A-101,\"Pune, MH\",2023-08-01,x,\"{9000000001}\",\"{9100000001}\",1,2026-09-01 00:00:00+00\n", fs);
    fputs("612415096,Shivani Mahajan,TY CSE,A-102,\"Pune, MH\",2023-08-01,x,\"{9000000002}\",\"{9100000002}\",1,2026-09-01 00:00:00+00\n", fs);
    fputs("612415101,Aarohi Patil,SY ENTC,B-204,\"Nashik, MH\",2024-08-02,x,\"{9000000003}\",\"{9100000003}\",1,2026-09-01 00:00:00+00\n", fs);
    fputs("612415117,Meera Joshi,FY Mech,B-210,\"Satara, MH\",2025-08-03,x,\"{9000000004}\",\"{9100000004}\",1,2026-09-01 00:00:00+00\n", fs);
    fputs("612415123,Ishita Deshmukh,LY CSE,C-305,\"Pune, MH\",2022-08-01,x,\"{9000000005}\",\"{9100000005}\",1,2026-09-01 00:00:00+00\n", fs);

    char (*student_mis_list)[16] = malloc((size_t)num_students * sizeof *student_mis_list);
    snprintf(student_mis_list[0], 16, "612415090");
    snprintf(student_mis_list[1], 16, "612415096");
    snprintf(student_mis_list[2], 16, "612415101");
    snprintf(student_mis_list[3], 16, "612415117");
    snprintf(student_mis_list[4], 16, "612415123");

    for (int i = 5; i < num_students; i++) {
        int mis_val = 612415000 + i + 150;
        snprintf(student_mis_list[i], 16, "%d", mis_val);
        const char *fn = FIRST_NAMES[rand_range(0, (uint32_t)NUM_FIRST - 1)];
        const char *ln = LAST_NAMES[rand_range(0, (uint32_t)NUM_LAST - 1)];
        const char *cls = CLASSES[rand_range(0, (uint32_t)NUM_CLASSES - 1)];
        char block = (char)('A' + rand_range(0, 3));
        int room_num = (int)(rand_range(1, 5) * 100 + rand_range(1, 20));
        const char *city = CITIES[rand_range(0, (uint32_t)NUM_CITIES - 1)];
        int adm_year = (int)rand_range(2022, 2025);
        int adm_month = (int)rand_range(7, 8);
        int adm_day = (int)rand_range(1, 28);
        int hostel_id = (int)rand_range(1, 5);
        long long phone = 9000000000LL + i;
        long long pphone = 9100000000LL + i;

        fprintf(fs, "%s,%s %s,%s,%c-%d,\"%s\",%04d-%02d-%02d,x,\"{%lld}\",\"{%lld}\",%d,2026-09-01 00:00:00+00\n",
                student_mis_list[i], fn, ln, cls, block, room_num, city, adm_year, adm_month, adm_day, phone, pphone, hostel_id);
    }
    fclose(fs);

    /* 5. entry_exit_log.csv */
    snprintf(path, sizeof path, "%s/entry_exit_log.csv", out_dir);
    FILE *fl = fopen(path, "w");
    if (!fl) { perror(path); return 1; }
    fputs("id,student_mis,in_out_time_id,log_date,exit_time,exit_latitude,exit_longitude,entry_time,entry_latitude,entry_longitude,entry_distance_m,status\n", fl);

    /* Original demo logs first */
    fputs("1,612415090,1,2026-10-26,2026-10-26 10:15:00+00,18.5293,73.8566,2026-10-26 13:40:00+00,18.5293,73.8566,40,IN\n", fl);
    fputs("2,612415096,1,2026-10-26,2026-10-26 11:00:00+00,18.5293,73.8566,2026-10-26 15:55:00+00,18.5293,73.8566,25,IN\n", fl);
    fputs("3,612415101,1,2026-10-26,2026-10-26 12:30:00+00,18.5293,73.8566,2026-10-26 16:20:00+00,18.5293,73.8566,60,IN\n", fl);
    fputs("4,612415117,1,2026-10-26,2026-10-26 14:00:00+00,18.5293,73.8566,2026-10-26 15:50:00+00,18.5293,73.8566,35,IN\n", fl);
    fputs("5,612415123,1,2026-10-26,2026-10-26 14:30:00+00,18.5293,73.8566,2026-10-26 16:45:00+00,18.5293,73.8566,90,IN\n", fl);
    fputs("6,612415090,1,2026-10-27,2026-10-27 09:00:00+00,18.5293,73.8566,2026-10-27 15:50:00+00,18.5293,73.8566,20,IN\n", fl);
    fputs("7,612415096,1,2026-10-27,2026-10-27 13:20:00+00,18.5293,73.8566,2026-10-27 16:10:00+00,18.5293,73.8566,45,IN\n", fl);
    fputs("8,612415101,1,2026-10-27,2026-10-27 14:05:00+00,18.5293,73.8566,,,,,OUT\n", fl);

    for (int i = 9; i <= num_logs; i++) {
        const char *mis = student_mis_list[rand_range(0, (uint32_t)num_students - 1)];
        int in_out_id = (int)rand_range(1, 5);
        int day = (int)rand_range(1, 27);
        int exit_h = (int)rand_range(7, 18);
        int exit_m = (int)rand_range(0, 59);
        int duration_m = (int)rand_range(30, 300);
        int entry_total_m = exit_h * 60 + exit_m + duration_m;
        int entry_h = entry_total_m / 60;
        int entry_m = entry_total_m % 60;
        int dist = (int)rand_range(10, 100);

        /* 3% probability of being OUT currently (especially on day 27) */
        int is_out = (rand_range(0, 100) < 3) || (day == 27 && exit_h >= 14 && rand_range(0, 5) == 0);

        if (is_out) {
            fprintf(fl, "%d,%s,%d,2026-10-%02d,2026-10-%02d %02d:%02d:00+00,18.5293,73.8566,,,,,OUT\n",
                    i, mis, in_out_id, day, day, exit_h, exit_m);
        } else {
            fprintf(fl, "%d,%s,%d,2026-10-%02d,2026-10-%02d %02d:%02d:00+00,18.5293,73.8566,2026-10-%02d %02d:%02d:00+00,18.5293,73.8566,%d,IN\n",
                    i, mis, in_out_id, day, day, exit_h, exit_m, day, entry_h, entry_m, dist);
        }
    }
    fclose(fl);

    /* 6. alert.csv */
    snprintf(path, sizeof path, "%s/alert.csv", out_dir);
    FILE *fa = fopen(path, "w");
    if (!fa) { perror(path); return 1; }
    fputs("alert_id,log_id,warden_id,alert_type,alert_time,message,status\n", fa);
    fputs("1,8,1,NOT_RETURNED,2026-10-27 15:30:00+00,Aarohi Patil (612415101) has not returned,OPEN\n", fa);
    for (int i = 2; i <= 50; i++) {
        int log_id = (int)rand_range(9, (uint32_t)num_logs);
        int warden_id = (int)rand_range(1, 5);
        int day = (int)rand_range(1, 27);
        int hour = (int)rand_range(21, 23);
        int minute = (int)rand_range(0, 59);
        const char *type = (i % 3 == 0) ? "CURFEW_BREACH" : "NOT_RETURNED";
        const char *st = (i % 2 == 0) ? "OPEN" : "RESOLVED";
        fprintf(fa, "%d,%d,%d,%s,2026-10-%02d %02d:%02d:00+00,Student log %d alert logged,%s\n",
                i, log_id, warden_id, type, day, hour, minute, log_id, st);
    }
    fclose(fa);

    free(student_mis_list);
    printf("Successfully seeded data to %s:\n", out_dir);
    printf("  - %d hostels\n", 5);
    printf("  - %d wardens\n", 5);
    printf("  - %d students\n", num_students);
    printf("  - %d entry_exit_logs\n", num_logs);
    printf("  - 50 alerts\n");
    return 0;
}
