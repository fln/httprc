define([
        'angular',
	'moment',
        //'sweetalert',
        'angular-resource',
        'angular-ui-router'
], function (angular, moment) {
'use strict';

var app = angular.module('httprcDemo', ['ngResource', 'ui.router']);

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
			/*'deviceState': ['$resource', '$stateParams', function($resource, $stateParams) {
				return $resource('/v1/httprc/client/' + $stateParams.deviceID),get();
			}]*/
		resolve: {
			'deviceState': ['$resource', '$stateParams', function($resource, $stateParams) {
				return $resource('v1/httprc/client/' + $stateParams.deviceID).get();
			}]
		},
		controller: ['$stateParams', '$resource', 'deviceState', function($stateParams, $resource, deviceState) {
			this.state = deviceState;
			this.deviceID = $stateParams.deviceID;
			this.seenAgo = moment(deviceState.lastSeen.time).fromNow();

			this.newCmd = {
				command: "",
				stdin: "",
				waitTimeMs: 10000,
				outputBufferSize: 1048576
			};

			this.reload = function() {
				this.state = $resource('v1/httprc/client/' + $stateParams.deviceID).get();
				console.log(this.state);
				this.state.$promise.then(function (data) {
					this.seenAgo = moment(this.state.lastSeen.time).fromNow();
				}.bind(this));
			};

			this.scheduleNewCommand = function() {
				var r = $resource('v1/httprc/client/' + this.deviceID + "/task");
				r.save(this.newCmd)
				console.log(this.newCmd)
			};
		}],
		controllerAs: 'device'
	});
}]);

});
