Setup:

- Clone the repository on your home folder (e.g. /home/username/).
- Create a crontab task by typing 'crontab -e' and then '@reboot /home/username/redundancy_demo/boot.sh' at the bottom of the file
- Edit the boot.sh script to include the computer's address, the peer's address, and the pilotlight app's address (e.g. /server 192.168.51.168 192.168.51.170 192.168.51.182)
- (NOTE: One and only one of the computers must be declared as the primary computer by adding -p to the boot command: /server -p 192.168.51.168 192.168.51.170 192.168.51.182)
- Run the make.sh script and you're done!

