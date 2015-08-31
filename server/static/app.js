define([
        'angular',
	'moment',
        //'sweetalert',
        'angular-resource',
        'angular-ui-router'
], function (angular, moment) {
'use strict';

var app = angular.module('httprcDemo', ['ngResource', 'ui.router']);

app.directive('msAsSec', function() {
	return {
		restrict: 'A',
		require: 'ngModel',
		link: function(scope, element, attr, ngModel) {
			ngModel.$parsers.push(function (data) {
				return (data || 0) * 1000;
			});
			ngModel.$formatters.push(function (data) {
				return (data || 0) / 1000;
			});
		}
	};
});

app.directive('bytesAsKb', function() {
	return {
		restrict: 'A',
		require: 'ngModel',
		link: function(scope, element, attr, ngModel) {
			ngModel.$parsers.push(function (data) {
				return (data || 0) * 1024;
			});
			ngModel.$formatters.push(function (data) {
				return (data || 0) / 1024;
			});
		}
	};
});


app.filter('moment_fromNow', function() {
	return function (data) {
		return moment(data).fromNow();
	};
});

app.filter('moment_duration', function() {
	return function (data) {
		if (data < 1000) {
			return data + " millisecond"
		} else {
			return moment.duration(data).humanize();
		}
	};
});

app.filter('rcToString', function() {
	return function (data) {
		if (data == 0) {
			return "success";
		} else {
			return "failure";
		}
	};
});


app.directive('addSpace', [function () {
        return function (scope, element) {
            if(!scope.$last){
                element.after('&nbsp;');
            }
        }
    }
]);

app.config([     '$stateProvider','$locationProvider',
        function($stateProvider,   $locationProvider) {

	//$locationProvider.html5Mode(true);
	$stateProvider.state('list', {
		url: '',
		templateUrl: 'list.partial.html',
		resolve: {
			'deviceList': ['$resource', function($resource) {
				return $resource('v1/httprc/clientList').query();
			}]
		},
		controller: ['deviceList', function(deviceList) {
			this.devices = deviceList;
		}],
		controllerAs: 'list'
	}).state('device', {
		url: '/device/{deviceID}',
		templateUrl: 'device.partial.html',
		resolve: {
			'deviceState': ['$resource', '$stateParams', function($resource, $stateParams) {
				return $resource('v1/httprc/client/' + $stateParams.deviceID).get().$promise;
			}]
		},
		controller: ['$scope', '$stateParams', '$resource', 'deviceState', function($scope, $stateParams, $resource, deviceState) {
			this.state = deviceState;
			this.deviceID = $stateParams.deviceID;
			//console.log(deviceState);
			this.seenAgo = moment(deviceState.lastSeen.time).fromNow();

			this.newCmd = {
				command: "",
				args: [],
				cleanEnvironment: false,
				environment: {},
				stdin: "",
				waitTimeMs: 10000,
				outputBufferSize: 1048576
			};
			this.tmpArg = "";

			this.reload = function() {
				this.state = $resource('v1/httprc/client/' + $stateParams.deviceID).get();
				this.state.$promise.then(function (data) {
					this.seenAgo = moment(this.state.lastSeen.time).fromNow();
				}.bind(this));
			};

			this.appendArg = function() {
				this.newCmd.args.push(this.tmpArg);
				this.tmpArg = '';
			}

			this.removeEnv = function(key) {
				delete this.newCmd.environment[key];
			}

			this.scheduleNewCommand = function() {
				var r = $resource('v1/httprc/client/' + this.deviceID + "/task");
				r.save(this.newCmd)
				console.log(this.newCmd)
			};

			this.handleNewFinished = function(data) {
			}

			var source = new EventSource('v1/httprc/client/' + $stateParams.deviceID + '/events');
			source.addEventListener('result', function (msg) {
				$scope.$apply(function () {
					var finishedTask = JSON.parse(msg.data);
					this.state.finished.push(finishedTask);
					for (var i = 0; i < this.state.pending.length; i += 1) {
						if (this.state.pending[i].id == finishedTask.command.id) {
							this.state.pending.splice(i, 1)
							break;
						}
					}
				}.bind(this));	
			}.bind(this), false);

			source.addEventListener('command', function (msg) {
				$scope.$apply(function () {
					this.state.pending.push(JSON.parse(msg.data));
				}.bind(this));	
			}.bind(this), false);

		}],
		controllerAs: 'device'
	});
}]);

});
